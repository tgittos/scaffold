#ifndef RALPH_H
#define RALPH_H

#include "http_client.h"
#include "conversation_tracker.h"
#include "tools_system.h"
#include "todo_manager.h"
#include "llm_provider.h"
#include "token_manager.h"
#include "session_manager.h"
#include "../mcp/mcp_client.h"
#include "../tools/subagent_tool.h"
#include "../utils/uuid_utils.h"
#include "../policy/approval_gate.h"

// API type enumeration - used for provider-specific behavior
typedef enum {
    API_TYPE_OPENAI,
    API_TYPE_ANTHROPIC,
    API_TYPE_LOCAL
} APIType;

// Ralph session structure - uses SessionData for core session functionality
typedef struct {
    char session_id[40];                 // UUID v4 unique session identifier
    SessionData session_data;            // Core session data (config, conversation, tool_count)
    TodoList todo_list;
    ToolRegistry tools;
    MCPClient mcp_client;                // Model Context Protocol client
    SubagentManager subagent_manager;    // Subagent process management
    ApprovalGateConfig gate_config;      // Approval gates for user confirmation
} RalphSession;

// Core Ralph functions
int ralph_init_session(RalphSession* session);
void ralph_cleanup_session(RalphSession* session);

int ralph_load_config(RalphSession* session);
int ralph_process_message(RalphSession* session, const char* user_message);

// Internal helper functions (exposed for testing)
char* ralph_build_json_payload(const char* model, const char* system_prompt, 
                              const ConversationHistory* conversation, 
                              const char* user_message, const char* max_tokens_param, 
                              int max_tokens, const ToolRegistry* tools);
char* ralph_build_anthropic_json_payload(const char* model, const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message, int max_tokens,
                                        const ToolRegistry* tools);

// Common tool calling workflow
int ralph_execute_tool_workflow(RalphSession* session, ToolCall* tool_calls, int call_count, 
                               const char* user_message, int max_tokens, const char** headers);

// Wrapper functions that automatically use enhanced system prompt with todo list
char* ralph_build_json_payload_with_todos(const RalphSession* session,
                                         const char* user_message, int max_tokens);
char* ralph_build_anthropic_json_payload_with_todos(const RalphSession* session,
                                                   const char* user_message, int max_tokens);

// Manage conversation size using compaction when needed
int manage_conversation_tokens(RalphSession* session, const char* user_message,
                              TokenConfig* config, TokenUsage* usage);

// Generate a recap of recent conversation without persisting to history
// Returns 0 on success, -1 on failure
// max_messages: maximum number of recent messages to include in recap context (0 for default)
int ralph_generate_recap(RalphSession* session, int max_messages);

#endif // RALPH_H