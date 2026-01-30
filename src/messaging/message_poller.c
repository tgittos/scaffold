#include "message_poller.h"
#include "../db/message_store.h"
#include "../utils/pipe_notifier.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <fcntl.h>

struct message_poller {
    char* agent_id;
    int poll_interval_ms;
    PipeNotifier notifier;
    pthread_t thread;
    atomic_int running;
    atomic_int has_pending;
    pending_message_counts_t last_counts;
    pthread_mutex_t counts_mutex;
};

static void* poller_thread_func(void* arg) {
    message_poller_t* poller = (message_poller_t*)arg;

    while (atomic_load(&poller->running)) {
        int sleep_ms = poller->poll_interval_ms;
        while (sleep_ms > 0 && atomic_load(&poller->running)) {
            int chunk = (sleep_ms > 100) ? 100 : sleep_ms;
            usleep(chunk * 1000);
            sleep_ms -= chunk;
        }

        if (!atomic_load(&poller->running)) {
            break;
        }

        /* Re-fetch store each iteration to handle singleton reset */
        message_store_t* store = message_store_get_instance();
        if (store == NULL) {
            continue;
        }

        int direct_pending = message_has_pending(store, poller->agent_id);
        int channel_pending = channel_has_pending(store, poller->agent_id);

        if (direct_pending > 0 || channel_pending > 0) {
            pthread_mutex_lock(&poller->counts_mutex);
            poller->last_counts.direct_count = (direct_pending > 0) ? direct_pending : 0;
            poller->last_counts.channel_count = (channel_pending > 0) ? channel_pending : 0;
            pthread_mutex_unlock(&poller->counts_mutex);

            /* Always send notification when messages are pending, even if has_pending is set.
             * This prevents a race where clear_notification drains the pipe but then
             * this thread sets has_pending, leaving pipe empty but flag set. */
            if (pipe_notifier_send(&poller->notifier, 'M') == 0) {
                atomic_store(&poller->has_pending, 1);
            }
        }
    }

    return NULL;
}

message_poller_t* message_poller_create(const char* agent_id, int poll_interval_ms) {
    if (agent_id == NULL) {
        return NULL;
    }

    message_poller_t* poller = calloc(1, sizeof(message_poller_t));
    if (poller == NULL) {
        return NULL;
    }

    poller->agent_id = strdup(agent_id);
    if (poller->agent_id == NULL) {
        free(poller);
        return NULL;
    }

    poller->poll_interval_ms = (poll_interval_ms > 0) ? poll_interval_ms : MESSAGE_POLLER_DEFAULT_INTERVAL_MS;
    poller->notifier.read_fd = -1;
    poller->notifier.write_fd = -1;
    atomic_store(&poller->running, 0);
    atomic_store(&poller->has_pending, 0);

    if (pthread_mutex_init(&poller->counts_mutex, NULL) != 0) {
        free(poller->agent_id);
        free(poller);
        return NULL;
    }

    if (pipe_notifier_init(&poller->notifier) != 0) {
        pthread_mutex_destroy(&poller->counts_mutex);
        free(poller->agent_id);
        free(poller);
        return NULL;
    }

    return poller;
}

void message_poller_destroy(message_poller_t* poller) {
    if (poller == NULL) {
        return;
    }

    message_poller_stop(poller);

    pipe_notifier_destroy(&poller->notifier);

    pthread_mutex_destroy(&poller->counts_mutex);
    free(poller->agent_id);
    free(poller);
}

int message_poller_start(message_poller_t* poller) {
    if (poller == NULL) {
        return -1;
    }

    if (atomic_load(&poller->running)) {
        return 0;
    }

    atomic_store(&poller->running, 1);

    if (pthread_create(&poller->thread, NULL, poller_thread_func, poller) != 0) {
        atomic_store(&poller->running, 0);
        return -1;
    }

    return 0;
}

void message_poller_stop(message_poller_t* poller) {
    if (poller == NULL) {
        return;
    }

    if (!atomic_load(&poller->running)) {
        return;
    }

    atomic_store(&poller->running, 0);
    pthread_join(poller->thread, NULL);
}

int message_poller_get_notify_fd(message_poller_t* poller) {
    if (poller == NULL) {
        return -1;
    }
    return pipe_notifier_get_read_fd(&poller->notifier);
}

int message_poller_get_pending(message_poller_t* poller, pending_message_counts_t* counts) {
    if (poller == NULL || counts == NULL) {
        return -1;
    }

    pthread_mutex_lock(&poller->counts_mutex);
    counts->direct_count = poller->last_counts.direct_count;
    counts->channel_count = poller->last_counts.channel_count;
    pthread_mutex_unlock(&poller->counts_mutex);

    return 0;
}

int message_poller_clear_notification(message_poller_t* poller) {
    if (poller == NULL) {
        return -1;
    }

    pipe_notifier_drain(&poller->notifier);

    atomic_store(&poller->has_pending, 0);

    pthread_mutex_lock(&poller->counts_mutex);
    poller->last_counts.direct_count = 0;
    poller->last_counts.channel_count = 0;
    pthread_mutex_unlock(&poller->counts_mutex);

    return 0;
}
