#include "async_executor.h"
#include "interrupt.h"
#include "debug_output.h"
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
 * Thread-safety: Only written once during creation, read from executor thread.
 */
static async_executor_t* g_active_executor = NULL;

struct async_executor {
    RalphSession* session;
    int pipe_fds[2];
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    atomic_int running;
    atomic_int cancel_requested;
    atomic_int thread_exited;  /* Set when thread function has fully completed */
    char* current_message;
    int last_result;
    char* last_error;
};

static void send_event(async_executor_t* executor, AsyncEventType event) {
    char c = (char)event;
    ssize_t written = write(executor->pipe_fds[1], &c, 1);
    if (written != 1) {
        debug_printf("async_executor: Failed to write event %c to pipe: %s\n",
                     c, strerror(errno));
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

    int result = ralph_process_message(executor->session, executor->current_message);

    pthread_mutex_lock(&executor->mutex);
    executor->last_result = result;
    if (executor->last_error != NULL) {
        free(executor->last_error);
        executor->last_error = NULL;
    }

    if (atomic_load(&executor->cancel_requested)) {
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

    /* Signal that thread has fully completed all work.
     * This must be the last operation before return to prevent
     * the main thread from freeing the executor while we're still
     * accessing it. */
    atomic_store(&executor->thread_exited, 1);

    return NULL;
}

async_executor_t* async_executor_create(RalphSession* session) {
    if (session == NULL) {
        return NULL;
    }

    async_executor_t* executor = calloc(1, sizeof(async_executor_t));
    if (executor == NULL) {
        return NULL;
    }

    executor->session = session;
    executor->pipe_fds[0] = -1;
    executor->pipe_fds[1] = -1;
    atomic_store(&executor->running, 0);
    atomic_store(&executor->cancel_requested, 0);
    atomic_store(&executor->thread_exited, 1);  /* No thread running yet */
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

    if (pipe(executor->pipe_fds) != 0) {
        pthread_cond_destroy(&executor->cond);
        pthread_mutex_destroy(&executor->mutex);
        free(executor);
        return NULL;
    }

    int flags = fcntl(executor->pipe_fds[0], F_GETFL, 0);
    if (flags != -1) {
        fcntl(executor->pipe_fds[0], F_SETFL, flags | O_NONBLOCK);
    }
    flags = fcntl(executor->pipe_fds[1], F_GETFL, 0);
    if (flags != -1) {
        fcntl(executor->pipe_fds[1], F_SETFL, flags | O_NONBLOCK);
    }

    g_active_executor = executor;

    debug_printf("async_executor: Created with notify fd %d\n", executor->pipe_fds[0]);
    return executor;
}

void async_executor_destroy(async_executor_t* executor) {
    if (executor == NULL) {
        return;
    }

    if (g_active_executor == executor) {
        g_active_executor = NULL;
    }

    if (atomic_load(&executor->running)) {
        async_executor_cancel(executor);
        async_executor_wait(executor);
    }

    if (executor->pipe_fds[0] >= 0) {
        close(executor->pipe_fds[0]);
    }
    if (executor->pipe_fds[1] >= 0) {
        close(executor->pipe_fds[1]);
    }

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
    atomic_store(&executor->thread_exited, 0);  /* Thread about to start */
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

    pthread_detach(executor->thread);

    debug_printf("async_executor: Started processing message\n");
    return 0;
}

int async_executor_get_notify_fd(async_executor_t* executor) {
    if (executor == NULL) {
        return -1;
    }
    return executor->pipe_fds[0];
}

int async_executor_process_events(async_executor_t* executor) {
    if (executor == NULL) {
        return -1;
    }

    char event;
    ssize_t n = read(executor->pipe_fds[0], &event, 1);
    if (n != 1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }

    debug_printf("async_executor: Received event '%c'\n", event);
    return (int)event;
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

    /* Spin-wait briefly for the detached thread to fully exit.
     * This prevents a race where we free the executor while the
     * thread is still in its return path after setting running=0. */
    int spin_count = 0;
    while (!atomic_load(&executor->thread_exited) && spin_count < 1000) {
        usleep(100);  /* 100 microseconds */
        spin_count++;
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
    return g_active_executor;
}
