#ifndef RALPH_H
#define RALPH_H

#include "http_client.h"
#include "conversation_tracker.h"
#include "tools_system.h"

// Ralph configuration structure
typedef struct {
    char* api_url;
    char* model;
    char* api_key;
    char* system_prompt;
    int context_window;
    int max_tokens;
    const char* max_tokens_param;
} RalphConfig;

// Ralph session structure
typedef struct {
    RalphConfig config;
    ConversationHistory conversation;
    ToolRegistry tools;
} RalphSession;

// Core Ralph functions
int ralph_init_session(RalphSession* session);
void ralph_cleanup_session(RalphSession* session);

int ralph_load_config(RalphSession* session);
int ralph_process_message(RalphSession* session, const char* user_message);

// Internal helper functions (exposed for testing)
char* ralph_escape_json_string(const char* str);
char* ralph_build_json_payload(const char* model, const char* system_prompt, 
                              const ConversationHistory* conversation, 
                              const char* user_message, const char* max_tokens_param, 
                              int max_tokens, const ToolRegistry* tools);

// Common tool calling workflow
int ralph_execute_tool_workflow(RalphSession* session, ToolCall* tool_calls, int call_count, 
                               const char* user_message, int max_tokens, const char** headers);

#endif // RALPH_H