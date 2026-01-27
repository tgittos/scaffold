#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <stdbool.h>
#include "conversation_tracker.h"

typedef struct {
    char* api_url;
    char* model;
    char* api_key;
    char* system_prompt;
    int context_window;
    int max_tokens;
    const char* max_tokens_param;
    int api_type;  // 0=OpenAI, 1=Anthropic, 2=Local
    bool enable_streaming;
    bool json_output_mode;
} SessionConfig;

typedef struct {
    SessionConfig config;
    ConversationHistory conversation;
    int tool_count;
} SessionData;

void session_data_init(SessionData* session);
void session_data_cleanup(SessionData* session);

#endif // SESSION_MANAGER_H