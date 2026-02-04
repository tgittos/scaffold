#include "async_executor.h"
#include "../util/interrupt.h"
#include "../util/debug_output.h"
#include "../ipc/pipe_notifier.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <fcntl.h>
#include <errno.h>

/**
 * Global executor pointer for use by subagent_spawn notification.
 * Set when executor is created in interactive mode.
 * Thread-safety: Protected by g_executor_mutex for all accesses.
 */
static async_executor_t* g_active_executor = NULL;
static pthread_mutex_t g_executor_mutex;
static pthread_once_t g_executor_mutex_once = PTHREAD_ONCE_INIT;

static void init_executor_mutex(void) {
    pthread_mutex_init(&g_executor_mutex, NULL);
}

static void ensure_executor_mutex_initialized(void) {
    pthread_once(&g_executor_mutex_once, init_executor_mutex);
}

struct async_executor {
    AgentSession* session;
    PipeNotifier notifier;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    atomic_int running;
    atomic_int cancel_requested;
    atomic_int thread_started;  /* Set when thread has been created */
    char* current_message;
    int last_result;
    char* last_error;
};

static void send_event(async_executor_t* executor, AsyncEventType event) {
    if (pipe_notifier_send(&executor->notifier, (char)event) != 0) {
        debug_printf("async_executor: Failed to write event %c to pipe: %s\n",
                     (char)event, strerror(errno));
    }
}

static void* executor_thread_func(void* arg) {
    async_executor_t* executor = (async_executor_t*)arg;

    debug_printf("async_executor: Thread started for message: %.50s...\n",
                 executor->current_message ? executor->current_message : "(null)");

    if (executor->current_message == NULL) {
        pthread_mutex_lock(&executor->mutex);
        executor->last_result = -1;
        executor->last_error = strdup("No message to process");
        pthread_mutex_unlock(&executor->mutex);
        send_event(executor, ASYNC_EVENT_ERROR);
        atomic_store(&executor->running, 0);
        return NULL;
    }

    int result = session_process_message(executor->session, executor->current_message);

    pthread_mutex_lock(&executor->mutex);
    executor->last_result = result;
    if (executor->last_error != NULL) {
        free(executor->last_error);
        executor->last_error = NULL;
    }

    if (atomic_load(&executor->cancel_requested) || result == -2) {
        debug_printf("async_executor: Execution was cancelled\n");
        send_event(executor, ASYNC_EVENT_INTERRUPTED);
    } else if (result != 0) {
        debug_printf("async_executor: Execution failed with result %d\n", result);
        executor->last_error = strdup("Message processing failed");
        send_event(executor, ASYNC_EVENT_ERROR);
    } else {
        debug_printf("async_executor: Execution completed successfully\n");
        send_event(executor, ASYNC_EVENT_COMPLETE);
    }

    free(executor->current_message);
    executor->current_message = NULL;
    pthread_mutex_unlock(&executor->mutex);

    atomic_store(&executor->running, 0);
    pthread_cond_broadcast(&executor->cond);

    return NULL;
}

async_executor_t* async_executor_create(AgentSession* session) {
    if (session == NULL) {
        return NULL;
    }

    async_executor_t* executor = calloc(1, sizeof(async_executor_t));
    if (executor == NULL) {
        return NULL;
    }

    executor->session = session;
    executor->notifier.read_fd = -1;
    executor->notifier.write_fd = -1;
    atomic_store(&executor->running, 0);
    atomic_store(&executor->cancel_requested, 0);
    atomic_store(&executor->thread_started, 0);
    executor->current_message = NULL;
    executor->last_result = 0;
    executor->last_error = NULL;

    if (pthread_mutex_init(&executor->mutex, NULL) != 0) {
        free(executor);
        return NULL;
    }

    if (pthread_cond_init(&executor->cond, NULL) != 0) {
        pthread_mutex_destroy(&executor->mutex);
        free(executor);
        return NULL;
    }

    if (pipe_notifier_init(&executor->notifier) != 0) {
        pthread_cond_destroy(&executor->cond);
        pthread_mutex_destroy(&executor->mutex);
        free(executor);
        return NULL;
    }

    ensure_executor_mutex_initialized();
    pthread_mutex_lock(&g_executor_mutex);
    g_active_executor = executor;
    pthread_mutex_unlock(&g_executor_mutex);

    debug_printf("async_executor: Created with notify fd %d\n", executor->notifier.read_fd);
    return executor;
}

void async_executor_destroy(async_executor_t* executor) {
    if (executor == NULL) {
        return;
    }

    ensure_executor_mutex_initialized();
    pthread_mutex_lock(&g_executor_mutex);
    if (g_active_executor == executor) {
        g_active_executor = NULL;
    }
    pthread_mutex_unlock(&g_executor_mutex);

    /* Cancel any running execution and wait for thread to finish */
    if (atomic_load(&executor->running)) {
        async_executor_cancel(executor);
    }

    /* Join the thread if it was ever started */
    if (atomic_load(&executor->thread_started)) {
        pthread_join(executor->thread, NULL);
        atomic_store(&executor->thread_started, 0);
    }

    pipe_notifier_destroy(&executor->notifier);

    pthread_cond_destroy(&executor->cond);
    pthread_mutex_destroy(&executor->mutex);

    free(executor->current_message);
    free(executor->last_error);
    free(executor);

    debug_printf("async_executor: Destroyed\n");
}

int async_executor_start(async_executor_t* executor, const char* message) {
    if (executor == NULL || message == NULL) {
        return -1;
    }

    if (atomic_load(&executor->running)) {
        debug_printf("async_executor: Cannot start, already running\n");
        return -1;
    }

    /* Join any previous thread before starting a new one */
    if (atomic_load(&executor->thread_started)) {
        pthread_join(executor->thread, NULL);
        atomic_store(&executor->thread_started, 0);
    }

    pthread_mutex_lock(&executor->mutex);

    free(executor->current_message);
    executor->current_message = strdup(message);
    if (executor->current_message == NULL) {
        pthread_mutex_unlock(&executor->mutex);
        return -1;
    }

    free(executor->last_error);
    executor->last_error = NULL;
    executor->last_result = 0;
    atomic_store(&executor->cancel_requested, 0);
    atomic_store(&executor->running, 1);

    pthread_mutex_unlock(&executor->mutex);

    if (pthread_create(&executor->thread, NULL, executor_thread_func, executor) != 0) {
        pthread_mutex_lock(&executor->mutex);
        free(executor->current_message);
        executor->current_message = NULL;
        atomic_store(&executor->running, 0);
        pthread_mutex_unlock(&executor->mutex);
        return -1;
    }

    atomic_store(&executor->thread_started, 1);

    debug_printf("async_executor: Started processing message\n");
    return 0;
}

int async_executor_get_notify_fd(async_executor_t* executor) {
    if (executor == NULL) {
        return -1;
    }
    return pipe_notifier_get_read_fd(&executor->notifier);
}

int async_executor_process_events(async_executor_t* executor) {
    if (executor == NULL) {
        return -1;
    }

    char event;
    int result = pipe_notifier_recv(&executor->notifier, &event);
    if (result == 1) {
        debug_printf("async_executor: Received event '%c'\n", event);
        return (int)event;
    }
    if (result == 0) {
        return 0;  /* No event available */
    }
    return -1;  /* Error */
}

int async_executor_is_running(async_executor_t* executor) {
    if (executor == NULL) {
        return 0;
    }
    return atomic_load(&executor->running);
}

void async_executor_cancel(async_executor_t* executor) {
    if (executor == NULL) {
        return;
    }

    if (!atomic_load(&executor->running)) {
        return;
    }

    debug_printf("async_executor: Cancel requested\n");
    atomic_store(&executor->cancel_requested, 1);

    /* Also set the global interrupt flag so existing interrupt_pending() checks
     * in tool_executor.c and other places will trigger */
    interrupt_handler_trigger();
}

int async_executor_wait(async_executor_t* executor) {
    if (executor == NULL) {
        return -1;
    }

    pthread_mutex_lock(&executor->mutex);
    while (atomic_load(&executor->running)) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 30;

        int rc = pthread_cond_timedwait(&executor->cond, &executor->mutex, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&executor->mutex);
            debug_printf("async_executor: Wait timed out\n");
            return -1;
        }
    }
    pthread_mutex_unlock(&executor->mutex);

    /* Join the thread to ensure it has fully exited */
    if (atomic_load(&executor->thread_started)) {
        pthread_join(executor->thread, NULL);
        atomic_store(&executor->thread_started, 0);
    }

    return 0;
}

const char* async_executor_get_error(async_executor_t* executor) {
    if (executor == NULL) {
        return NULL;
    }
    return executor->last_error;
}

int async_executor_get_result(async_executor_t* executor) {
    if (executor == NULL) {
        return -1;
    }
    return executor->last_result;
}

void async_executor_notify_subagent_spawned(async_executor_t* executor) {
    if (executor == NULL) {
        return;
    }

    /* Only notify if executor is currently running, otherwise the main
     * thread isn't blocked in select() waiting for events */
    if (!atomic_load(&executor->running)) {
        return;
    }

    send_event(executor, ASYNC_EVENT_SUBAGENT_SPAWNED);
}

async_executor_t* async_executor_get_active(void) {
    ensure_executor_mutex_initialized();
    pthread_mutex_lock(&g_executor_mutex);
    async_executor_t* executor = g_active_executor;
    pthread_mutex_unlock(&g_executor_mutex);
    return executor;
}
