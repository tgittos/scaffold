#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "conversation_tracker.h"
#include "tools_system.h"

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

// Copy configuration from another session (for components that need config only)
// Returns 0 on success, -1 on failure (memory allocation error)
int session_data_copy_config(SessionData* dest, const SessionConfig* src);

// Build API request payload (abstracted from ralph.c)
char* session_build_api_payload(const SessionData* session, const char* user_message, 
                                int max_tokens, int include_tools);

// Make API request and return response (abstracted from ralph.c)
int session_make_api_request(const SessionData* session, const char* payload, 
                            char** response_content);

#endif // SESSION_MANAGER_H