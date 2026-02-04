#ifndef RALPH_H
#define RALPH_H

#include "http_client.h"
#include "conversation_tracker.h"
#include "lib/tools/tools_system.h"
#include "lib/tools/todo_manager.h"
#include "lib/llm/llm_provider.h"
#include "token_manager.h"
#include "session_manager.h"
#include "../mcp/mcp_client.h"
#include "lib/tools/subagent_tool.h"
#include "util/uuid_utils.h"
#include "../policy/approval_gate.h"
#include "ipc/message_poller.h"

typedef enum {
    API_TYPE_OPENAI,
    API_TYPE_ANTHROPIC,
    API_TYPE_LOCAL
} APIType;

typedef struct {
    int auto_poll_enabled;
    int poll_interval_ms;
} MessagePollingConfig;

typedef struct {
    char session_id[40];
    SessionData session_data;
    TodoList todo_list;
    ToolRegistry tools;
    MCPClient mcp_client;
    SubagentManager subagent_manager;
    ApprovalGateConfig gate_config;
    message_poller_t* message_poller;
    MessagePollingConfig polling_config;
} RalphSession;

int ralph_init_session(RalphSession* session);
void ralph_cleanup_session(RalphSession* session);

int ralph_load_config(RalphSession* session);

/**
 * Process a user message through the LLM and execute any tool calls.
 *
 * @param session The active Ralph session
 * @param user_message The message to process
 * @return 0 on success, -1 on error, -2 if interrupted by user (Ctrl+C)
 *
 * Return value -2 indicates the operation was cancelled by the user but the
 * session is still valid. A cancellation message has already been displayed
 * to the user, so callers should not print additional error messages.
 */
int ralph_process_message(RalphSession* session, const char* user_message);

char* ralph_build_json_payload(const char* model, const char* system_prompt,
                              const ConversationHistory* conversation, 
                              const char* user_message, const char* max_tokens_param, 
                              int max_tokens, const ToolRegistry* tools);
char* ralph_build_anthropic_json_payload(const char* model, const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message, int max_tokens,
                                        const ToolRegistry* tools);

int ralph_execute_tool_workflow(RalphSession* session, ToolCall* tool_calls, int call_count,
                               const char* user_message, int max_tokens, const char** headers);

char* ralph_build_json_payload_with_todos(const RalphSession* session,
                                         const char* user_message, int max_tokens);
char* ralph_build_anthropic_json_payload_with_todos(const RalphSession* session,
                                                   const char* user_message, int max_tokens);

int manage_conversation_tokens(RalphSession* session, const char* user_message,
                              TokenConfig* config, TokenUsage* usage);

int ralph_generate_recap(RalphSession* session, int max_messages);

int ralph_start_message_polling(RalphSession* session);
void ralph_stop_message_polling(RalphSession* session);

#endif // RALPH_H