/**
 * lib/agent/session.h - Agent Session Definition
 *
 * Defines the AgentSession structure which aggregates all the components
 * needed for an agent: session data, tools, MCP client, subagent manager,
 * approval gates, and message polling.
 *
 * This header is internal to lib/ and allows lib/ to be fully independent
 * of src/. External code should use lib/agent/agent.h instead.
 */

#ifndef LIB_AGENT_SESSION_H
#define LIB_AGENT_SESSION_H

#include "../session/session_manager.h"
#include "../tools/todo_manager.h"
#include "../tools/tools_system.h"
#include "../mcp/mcp_client.h"
#include "../tools/subagent_tool.h"
#include "../policy/approval_gate.h"
#include "../ipc/message_poller.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * API type for provider detection.
 */
typedef enum {
    API_TYPE_OPENAI = 0,
    API_TYPE_ANTHROPIC = 1,
    API_TYPE_LOCAL = 2
} APIType;

/**
 * Configuration for message polling in the session.
 */
typedef struct {
    int auto_poll_enabled;      /**< Whether to automatically poll for messages */
    int poll_interval_ms;       /**< Polling interval in milliseconds */
} SessionPollingConfig;

/**
 * The core session structure that holds all agent state.
 *
 * All constituent types are defined in lib/:
 * - SessionData: lib/session/session_manager.h
 * - TodoList: lib/tools/todo_manager.h
 * - ToolRegistry: lib/tools/tools_system.h
 * - MCPClient: lib/mcp/mcp_client.h
 * - SubagentManager: lib/tools/subagent_tool.h
 * - ApprovalGateConfig: lib/policy/approval_gate.h
 * - message_poller_t: lib/ipc/message_poller.h
 */
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
} AgentSession;

/* =============================================================================
 * CLEANUP HOOKS
 * Allows external code to register cleanup functions without lib/ knowing
 * about the specifics (e.g., Python interpreter shutdown).
 * ============================================================================= */

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

/* =============================================================================
 * SESSION LIFECYCLE
 * ============================================================================= */

/**
 * Initialize an agent session.
 * Sets up all subsystems: tools, MCP, subagents, approval gates, etc.
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

/* =============================================================================
 * MESSAGE POLLING
 * ============================================================================= */

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

/* =============================================================================
 * RECAP GENERATION
 * ============================================================================= */

/**
 * Generate a recap/summary of recent conversation for session greeting.
 *
 * @param session The session
 * @param max_messages Maximum number of recent messages to summarize
 * @return 0 on success, -1 on failure
 */
int session_generate_recap(AgentSession* session, int max_messages);

/* =============================================================================
 * PAYLOAD BUILDING
 * ============================================================================= */

/**
 * Build JSON payload for OpenAI-compatible APIs.
 *
 * @param session The session (used for config and conversation)
 * @param user_message The user's message
 * @param max_tokens Maximum tokens for response
 * @return Allocated JSON string (caller must free), or NULL on error
 */
char* session_build_json_payload(const AgentSession* session,
                                  const char* user_message, int max_tokens);

/**
 * Build JSON payload for Anthropic API.
 *
 * @param session The session
 * @param user_message The user's message
 * @param max_tokens Maximum tokens for response
 * @return Allocated JSON string (caller must free), or NULL on error
 */
char* session_build_anthropic_json_payload(const AgentSession* session,
                                            const char* user_message, int max_tokens);

/* =============================================================================
 * TOOL WORKFLOW
 * ============================================================================= */

/**
 * Execute a tool workflow (tool calls from LLM response).
 *
 * @param session The session
 * @param tool_calls Array of tool calls to execute
 * @param call_count Number of tool calls
 * @param user_message Original user message (for continuation)
 * @param max_tokens Max tokens for continuation responses
 * @param headers HTTP headers for API calls
 * @return 0 on success, non-zero on error
 */
int session_execute_tool_workflow(AgentSession* session, ToolCall* tool_calls,
                                   int call_count, const char* user_message,
                                   int max_tokens, const char** headers);

/* =============================================================================
 * TOKEN MANAGEMENT
 * ============================================================================= */

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
