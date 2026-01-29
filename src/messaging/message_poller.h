#ifndef MESSAGE_POLLER_H
#define MESSAGE_POLLER_H

#define MESSAGE_POLLER_DEFAULT_INTERVAL_MS 2000

typedef struct message_poller message_poller_t;

typedef struct {
    int direct_count;
    int channel_count;
} pending_message_counts_t;

message_poller_t* message_poller_create(const char* agent_id, int poll_interval_ms);
void message_poller_destroy(message_poller_t* poller);

int message_poller_start(message_poller_t* poller);
void message_poller_stop(message_poller_t* poller);

int message_poller_get_notify_fd(message_poller_t* poller);

int message_poller_get_pending(message_poller_t* poller, pending_message_counts_t* counts);

int message_poller_clear_notification(message_poller_t* poller);

#endif
