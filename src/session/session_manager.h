#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <stdbool.h>
#include "conversation_tracker.h"

// Lightweight session data structure for components
typedef struct {
    char* api_url;
    char* model;
    char* api_key;
    char* system_prompt;
    int context_window;
    int max_tokens;
    const char* max_tokens_param;
    int api_type;  // 0=OpenAI, 1=Anthropic, 2=Local
    bool enable_streaming;  // Default: true
    bool json_output_mode;  // Default: false
} SessionConfig;

typedef struct {
    SessionConfig config;
    ConversationHistory conversation;
    int tool_count;  // Simplified tool tracking
} SessionData;

// Initialize session data
void session_data_init(SessionData* session);

// Cleanup session data
void session_data_cleanup(SessionData* session);

#endif // SESSION_MANAGER_H