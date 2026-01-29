#ifndef MESSAGE_STORE_H
#define MESSAGE_STORE_H

#include <stddef.h>
#include <time.h>
#include "../utils/ptrarray.h"

typedef struct {
    char id[40];
    char sender_id[64];
    char recipient_id[64];
    char* content;
    time_t created_at;
    time_t read_at;
    time_t expires_at;
} DirectMessage;

typedef struct {
    char id[64];
    char* description;
    char creator_id[64];
    time_t created_at;
    int is_persistent;
} Channel;

typedef struct {
    char channel_id[64];
    char agent_id[64];
    time_t subscribed_at;
    time_t last_read_at;
} Subscription;

typedef struct {
    char id[40];
    char channel_id[64];
    char sender_id[64];
    char* content;
    time_t created_at;
} ChannelMessage;

PTRARRAY_DECLARE(DirectMessageArray, DirectMessage)
PTRARRAY_DECLARE(ChannelArray, Channel)
PTRARRAY_DECLARE(SubscriptionArray, Subscription)
PTRARRAY_DECLARE(ChannelMessageArray, ChannelMessage)

typedef struct message_store message_store_t;

message_store_t* message_store_get_instance(void);
void message_store_destroy(message_store_t* store);
void message_store_reset_instance(void);
message_store_t* message_store_create(const char* db_path);

// Direct messaging
// out_id must be at least 40 bytes. Pass 0 for ttl_seconds to disable expiry.
int message_send_direct(message_store_t* store, const char* sender_id,
                        const char* recipient_id, const char* content,
                        int ttl_seconds, char* out_id);

// Caller owns returned array and must free with direct_message_free_list().
// Messages are marked as read and will be cleaned up later.
DirectMessage** message_receive_direct(message_store_t* store, const char* agent_id,
                                       size_t max_count, size_t* out_count);

// Returns 1 if agent has pending messages, 0 if not, -1 on error.
int message_has_pending(message_store_t* store, const char* agent_id);

// Returns 1 if agent has pending channel messages (from subscribed channels), 0 if not, -1 on error.
int channel_has_pending(message_store_t* store, const char* agent_id);

// Caller owns returned message and must free with direct_message_free().
DirectMessage* message_get_direct(message_store_t* store, const char* message_id);

// Channels
int channel_create(message_store_t* store, const char* channel_name,
                   const char* description, const char* creator_id,
                   int is_persistent);

// Returns 0 on success, -1 on error. Caller owns returned Channel.
Channel* channel_get(message_store_t* store, const char* channel_name);

// Caller owns returned array and must free with channel_free_list().
Channel** channel_list(message_store_t* store, size_t* out_count);

int channel_delete(message_store_t* store, const char* channel_name);

// Subscriptions
int channel_subscribe(message_store_t* store, const char* channel_name,
                      const char* agent_id);

int channel_unsubscribe(message_store_t* store, const char* channel_name,
                        const char* agent_id);

// Returns 1 if subscribed, 0 if not, -1 on error.
int channel_is_subscribed(message_store_t* store, const char* channel_name,
                          const char* agent_id);

// Caller owns returned array and must free with channel_subscribers_free().
char** channel_get_subscribers(message_store_t* store, const char* channel_name,
                               size_t* out_count);

// Caller owns returned array and must free with channel_subscriptions_free().
char** channel_get_agent_subscriptions(message_store_t* store, const char* agent_id,
                                       size_t* out_count);

// Channel messages
// out_id must be at least 40 bytes.
int channel_publish(message_store_t* store, const char* channel_name,
                    const char* sender_id, const char* content, char* out_id);

// Get unread messages from a specific channel for an agent.
// Updates last_read_at after retrieval.
ChannelMessage** channel_receive(message_store_t* store, const char* channel_name,
                                 const char* agent_id, size_t max_count,
                                 size_t* out_count);

// Get unread messages from all subscribed channels for an agent.
// Updates last_read_at for each channel after retrieval.
ChannelMessage** channel_receive_all(message_store_t* store, const char* agent_id,
                                     size_t max_count, size_t* out_count);

// Cleanup functions
// Delete read messages older than grace_period_seconds.
int message_cleanup_read(message_store_t* store, int grace_period_seconds);

// Delete expired messages (based on expires_at).
int message_cleanup_expired(message_store_t* store);

// Delete all messages and subscriptions for an agent (when agent terminates).
int message_cleanup_agent(message_store_t* store, const char* agent_id);

// Delete old channel messages from non-persistent channels.
int message_cleanup_channel_messages(message_store_t* store, int max_age_seconds);

// Memory management
void direct_message_free(DirectMessage* msg);
void direct_message_free_list(DirectMessage** msgs, size_t count);
void channel_free(Channel* channel);
void channel_free_list(Channel** channels, size_t count);
void subscription_free(Subscription* sub);
void subscription_free_list(Subscription** subs, size_t count);
void channel_message_free(ChannelMessage* msg);
void channel_message_free_list(ChannelMessage** msgs, size_t count);
void channel_subscribers_free(char** subscribers, size_t count);
void channel_subscriptions_free(char** subscriptions, size_t count);

#endif // MESSAGE_STORE_H
