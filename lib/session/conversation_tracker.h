#ifndef CONVERSATION_TRACKER_H
#define CONVERSATION_TRACKER_H

#include "../util/darray.h"

typedef struct Services Services;

typedef struct {
    char *role;         // "user", "assistant", or "tool"
    char *content;
    char *tool_call_id; // nullable, only set for "tool" role
    char *tool_name;    // nullable, only set for "tool" role
} ConversationMessage;

DARRAY_DECLARE(ConversationHistory, ConversationMessage)

void conversation_tracker_set_services(Services* services);

int load_conversation_history(ConversationHistory *history);
int append_conversation_message(ConversationHistory *history, const char *role, const char *content);
int append_tool_message(ConversationHistory *history, const char *content, const char *tool_call_id, const char *tool_name);
void cleanup_conversation_history(ConversationHistory *history);
void init_conversation_history(ConversationHistory *history);

// days_back=0 means all history
int load_extended_conversation_history(ConversationHistory *history, int days_back, size_t max_messages);

// Caller owns returned history and must free with cleanup_conversation_history + free
ConversationHistory* search_conversation_history(const char *query, size_t max_results);

#endif // CONVERSATION_TRACKER_H