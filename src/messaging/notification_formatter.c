#include "notification_formatter.h"
#include "../db/message_store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INITIAL_CAPACITY 8
#define MAX_MESSAGES_PER_TYPE 20

static int bundle_add_message(notification_bundle_t* bundle, const char* sender_id,
                              const char* content, const char* channel_id, int is_channel) {
    if (bundle->count >= bundle->capacity) {
        size_t new_capacity = bundle->capacity * 2;
        notification_message_t* new_messages = realloc(bundle->messages,
                                                        new_capacity * sizeof(notification_message_t));
        if (new_messages == NULL) {
            return -1;
        }
        bundle->messages = new_messages;
        bundle->capacity = new_capacity;
    }

    notification_message_t* msg = &bundle->messages[bundle->count];
    msg->sender_id = sender_id ? strdup(sender_id) : NULL;
    msg->content = content ? strdup(content) : NULL;
    msg->channel_id = channel_id ? strdup(channel_id) : NULL;
    msg->is_channel_message = is_channel;

    if ((sender_id && !msg->sender_id) || (content && !msg->content) ||
        (channel_id && !msg->channel_id)) {
        free(msg->sender_id);
        free(msg->content);
        free(msg->channel_id);
        return -1;
    }

    bundle->count++;
    return 0;
}

notification_bundle_t* notification_bundle_create(const char* agent_id) {
    if (agent_id == NULL) {
        return NULL;
    }

    notification_bundle_t* bundle = calloc(1, sizeof(notification_bundle_t));
    if (bundle == NULL) {
        return NULL;
    }

    bundle->messages = calloc(INITIAL_CAPACITY, sizeof(notification_message_t));
    if (bundle->messages == NULL) {
        free(bundle);
        return NULL;
    }
    bundle->capacity = INITIAL_CAPACITY;
    bundle->count = 0;

    message_store_t* store = message_store_get_instance();
    if (store == NULL) {
        return bundle;
    }

    size_t direct_count = 0;
    DirectMessage** direct_msgs = message_receive_direct(store, agent_id, MAX_MESSAGES_PER_TYPE, &direct_count);
    if (direct_msgs != NULL) {
        for (size_t i = 0; i < direct_count; i++) {
            bundle_add_message(bundle, direct_msgs[i]->sender_id, direct_msgs[i]->content, NULL, 0);
        }
        direct_message_free_list(direct_msgs, direct_count);
    }

    size_t channel_count = 0;
    ChannelMessage** channel_msgs = channel_receive_all(store, agent_id, MAX_MESSAGES_PER_TYPE, &channel_count);
    if (channel_msgs != NULL) {
        for (size_t i = 0; i < channel_count; i++) {
            bundle_add_message(bundle, channel_msgs[i]->sender_id, channel_msgs[i]->content,
                               channel_msgs[i]->channel_id, 1);
        }
        channel_message_free_list(channel_msgs, channel_count);
    }

    return bundle;
}

void notification_bundle_destroy(notification_bundle_t* bundle) {
    if (bundle == NULL) {
        return;
    }

    for (size_t i = 0; i < bundle->count; i++) {
        free(bundle->messages[i].sender_id);
        free(bundle->messages[i].content);
        free(bundle->messages[i].channel_id);
    }
    free(bundle->messages);
    free(bundle);
}

char* notification_format_for_llm(const notification_bundle_t* bundle) {
    if (bundle == NULL || bundle->count == 0) {
        return NULL;
    }

    size_t total_size = 256;
    for (size_t i = 0; i < bundle->count; i++) {
        total_size += 128;
        if (bundle->messages[i].content) {
            total_size += strlen(bundle->messages[i].content);
        }
        if (bundle->messages[i].sender_id) {
            total_size += strlen(bundle->messages[i].sender_id);
        }
        if (bundle->messages[i].channel_id) {
            total_size += strlen(bundle->messages[i].channel_id);
        }
    }

    char* result = malloc(total_size);
    if (result == NULL) {
        return NULL;
    }

    char* ptr = result;
    int written = snprintf(ptr, total_size, "[INCOMING AGENT MESSAGES]\n\n");
    ptr += written;
    size_t remaining = total_size - written;

    for (size_t i = 0; i < bundle->count && remaining > 0; i++) {
        const notification_message_t* msg = &bundle->messages[i];
        int len;

        if (msg->is_channel_message) {
            len = snprintf(ptr, remaining, "Channel #%s from %s: \"%s\"\n",
                          msg->channel_id ? msg->channel_id : "unknown",
                          msg->sender_id ? msg->sender_id : "unknown",
                          msg->content ? msg->content : "");
        } else {
            len = snprintf(ptr, remaining, "Direct from %s: \"%s\"\n",
                          msg->sender_id ? msg->sender_id : "unknown",
                          msg->content ? msg->content : "");
        }

        if (len > 0 && (size_t)len < remaining) {
            ptr += len;
            remaining -= len;
        }
    }

    if (remaining > 64) {
        snprintf(ptr, remaining, "\nPlease review and respond to these messages.\n");
    }

    return result;
}

int notification_bundle_total_count(const notification_bundle_t* bundle) {
    if (bundle == NULL) {
        return 0;
    }
    return (int)bundle->count;
}
