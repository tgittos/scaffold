/* Internal to lib/. External code should use agent.h. */
#ifndef LIB_AGENT_SESSION_H
#define LIB_AGENT_SESSION_H

#include "../session/session_manager.h"
#include "../tools/todo_manager.h"
#include "../tools/tools_system.h"
#include "../mcp/mcp_client.h"
#include "prompt_mode.h"
#include "../tools/subagent_tool.h"
#include "../policy/approval_gate.h"
#include "../ipc/message_poller.h"
#include "../services/services.h"
#include "../plugin/plugin_manager.h"

/* Forward declaration — full definition in llm/model_capabilities.h */
typedef struct ModelRegistry ModelRegistry;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    API_TYPE_OPENAI = 0,
    API_TYPE_ANTHROPIC = 1,
    API_TYPE_LOCAL = 2
} APIType;

typedef struct {
    int auto_poll_enabled;      /**< Whether to automatically poll for messages */
    int poll_interval_ms;       /**< Polling interval in milliseconds */
} SessionPollingConfig;

typedef struct AgentSession {
    char session_id[40];                /**< UUID for this session */
    SessionData session_data;           /**< Configuration and conversation history */
    TodoList todo_list;                 /**< Task tracking */
    ToolRegistry tools;                 /**< Registered tools */
    MCPClient mcp_client;               /**< Model Context Protocol client */
    SubagentManager subagent_manager;   /**< Subagent process management */
    ApprovalGateConfig gate_config;     /**< Approval gates configuration */
    message_poller_t* message_poller;   /**< Background message poller thread */
    SessionPollingConfig polling_config;/**< Polling configuration */
    Services* services;                 /**< Dependency injection container */
    ModelRegistry* model_registry;      /**< Model capability registry */
    const char* model_override;         /**< Model override from --model flag */
    PromptMode current_mode;            /**< Active behavioral prompt mode */
    PluginManager plugin_manager;       /**< Plugin subprocess management */
} AgentSession;

/* Cleanup hooks: external code registers shutdown functions (e.g. Python interpreter). */

/**
 * Cleanup hook function signature.
 * Called during session_cleanup() before internal cleanup.
 */
typedef void (*SessionCleanupHook)(AgentSession *session);

/**
 * Register a cleanup hook to be called during session_cleanup().
 * Hooks are called in LIFO order (last registered = first called).
 *
 * @param hook Function to call during cleanup
 * @return 0 on success, -1 if max hooks reached
 */
int session_register_cleanup_hook(SessionCleanupHook hook);

/**
 * Unregister all cleanup hooks.
 * Called during final cleanup or testing.
 */
void session_unregister_all_hooks(void);

/**
 * Initialize an agent session.
 * Sets up all subsystems: tools, MCP, subagents, approval gates, etc.
 *
 * Precondition: session must be zero-initialized or have session->services
 * pre-set before calling. session_init does NOT reset the services pointer.
 *
 * @param session Session structure to initialize
 * @return 0 on success, -1 on failure
 */
int session_init(AgentSession* session);

/**
 * Clean up an agent session and free all resources.
 * Calls registered cleanup hooks, then cleans up internal subsystems.
 *
 * @param session Session to clean up
 */
void session_cleanup(AgentSession* session);

/**
 * Wire services into the session and check store availability.
 * Must be called after setting session->services and after session_init.
 *
 * @param session The session with services already set
 */
void session_wire_services(AgentSession* session);

/**
 * Load configuration for a session.
 * Loads API settings, system prompt, and model configuration.
 *
 * @param session The session
 * @return 0 on success, -1 on failure
 */
int session_load_config(AgentSession* session);

/**
 * Process a user message through the LLM and execute any tool calls.
 *
 * @param session The active session
 * @param user_message The message to process
 * @return 0 on success, -1 on error, -2 if interrupted by user (Ctrl+C)
 *
 * Return value -2 indicates the operation was cancelled by the user but the
 * session is still valid. A cancellation message has already been displayed
 * to the user, so callers should not print additional error messages.
 */
int session_process_message(AgentSession* session, const char* user_message);

/**
 * Continue the conversation without a new user message.
 * Triggers an LLM round-trip using the current conversation history as-is.
 * Use this after injecting a system message into conversation history.
 *
 * @param session The active session
 * @return 0 on success, -1 on error, -2 if interrupted by user (Ctrl+C)
 */
int session_continue(AgentSession* session);

/**
 * Start background message polling for the session.
 * Only starts if auto_poll_enabled is set in polling_config.
 *
 * @param session The session
 * @return 0 on success, -1 on failure
 */
int session_start_message_polling(AgentSession* session);

/**
 * Stop background message polling for the session.
 *
 * @param session The session
 */
void session_stop_message_polling(AgentSession* session);

/**
 * Generate a recap/summary of recent conversation for session greeting.
 *
 * @param session The session
 * @param max_messages Maximum number of recent messages to summarize
 * @return 0 on success, -1 on failure
 */
int session_generate_recap(AgentSession* session, int max_messages);

/**
 * Build JSON payload for OpenAI-compatible APIs.
 *
 * @param session The session (used for config and conversation)
 * @param user_message The user's message
 * @param max_tokens Maximum tokens for response
 * @return Allocated JSON string (caller must free), or NULL on error
 */
char* session_build_json_payload(AgentSession* session,
                                  const char* user_message, int max_tokens);

/**
 * Build JSON payload for Anthropic API.
 *
 * @param session The session
 * @param user_message The user's message
 * @param max_tokens Maximum tokens for response
 * @return Allocated JSON string (caller must free), or NULL on error
 */
char* session_build_anthropic_json_payload(AgentSession* session,
                                            const char* user_message, int max_tokens);

/**
 * Execute a tool workflow (tool calls from LLM response).
 *
 * @param session The session
 * @param tool_calls Array of tool calls to execute
 * @param call_count Number of tool calls
 * @param user_message Original user message (for continuation)
 * @param max_tokens Max tokens for continuation responses
 * @return 0 on success, non-zero on error
 */
int session_execute_tool_workflow(AgentSession* session, ToolCall* tool_calls,
                                   int call_count, const char* user_message,
                                   int max_tokens);

#include "../session/token_manager.h"

/**
 * Manage conversation token allocation with automatic compaction.
 *
 * @param session The session
 * @param user_message The user's message being processed
 * @param config Token configuration
 * @param usage Output token usage information
 * @return 0 on success, -1 on error
 */
int manage_conversation_tokens(AgentSession* session, const char* user_message,
                               TokenConfig* config, TokenUsage* usage);

#ifdef __cplusplus
}
#endif

#endif /* LIB_AGENT_SESSION_H */
