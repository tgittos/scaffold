#ifndef NOTIFICATION_FORMATTER_H
#define NOTIFICATION_FORMATTER_H

#include <stddef.h>
#include "../services/services.h"

typedef struct {
    char* sender_id;
    char* content;
    char* channel_id;
    int is_channel_message;
} notification_message_t;

typedef struct {
    notification_message_t* messages;
    size_t count;
    size_t capacity;
} notification_bundle_t;

notification_bundle_t* notification_bundle_create(const char* agent_id, Services* services);
void notification_bundle_destroy(notification_bundle_t* bundle);

char* notification_format_for_llm(const notification_bundle_t* bundle);

int notification_bundle_total_count(const notification_bundle_t* bundle);

#endif
