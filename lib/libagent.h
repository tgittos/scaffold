/**
 * libagent.h - Public API for the agent library
 *
 * This library provides agent creation, configuration, and execution.
 * The ralph CLI binary is a thin wrapper around this library.
 *
 * This header defines the public API for embedding agent functionality
 * in other programs or creating custom agent binaries.
 */

#ifndef LIBAGENT_H
#define LIBAGENT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * VERSION
 * ============================================================================= */

#define LIBAGENT_VERSION_MAJOR 0
#define LIBAGENT_VERSION_MINOR 1
#define LIBAGENT_VERSION_PATCH 0

/* =============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================= */

/* Core agent types */
typedef struct Agent Agent;
typedef struct AgentConfig AgentConfig;
typedef struct Services Services;

/* IPC types (lib/ipc/) */
typedef struct PipeNotifier PipeNotifier;
typedef struct AgentIdentity AgentIdentity;
typedef struct message_store message_store_t;

/* Tool types (lib/tools/) */
typedef struct ToolRegistry ToolRegistry;
typedef struct ToolCall ToolCall;
typedef struct ToolResult ToolResult;

/* UI types (lib/ui/) */
typedef struct ReplConfig ReplConfig;
typedef struct OutputConfig OutputConfig;

/* =============================================================================
 * AGENT MODULE
 *
 * Agent types and lifecycle are defined in lib/agent/agent.h.
 * Include that header (via this file) for the actual API.
 *
 * Key types:
 *   AgentMode   - INTERACTIVE, SINGLE_SHOT, or BACKGROUND
 *   AgentConfig - Configuration struct
 *   Agent       - Agent instance
 *
 * Key functions:
 *   agent_init()            - Initialize agent
 *   agent_load_config()     - Load configuration
 *   agent_run()             - Run based on mode
 *   agent_process_message() - Process single message
 *   agent_cleanup()         - Free resources
 * ============================================================================= */

/* =============================================================================
 * SERVICES MODULE (Dependency Injection)
 *
 * The Services module provides dependency injection for testability and
 * flexible configuration. Services can be the default singletons or custom.
 * ============================================================================= */

#include "services/services.h"

/* =============================================================================
 * IPC MODULE
 *
 * The IPC module provides inter-agent/inter-process communication primitives.
 * Include lib/ipc/ipc.h for full access.
 * ============================================================================= */

/*
 * IPC components are implemented in lib/ipc/.
 * For full documentation, see lib/ipc/ipc.h and the individual headers.
 *
 * Quick reference:
 *
 * PipeNotifier - Non-blocking pipe for async event notification
 *   pipe_notifier_init()
 *   pipe_notifier_destroy()
 *   pipe_notifier_send()
 *   pipe_notifier_recv()
 *   pipe_notifier_get_read_fd()
 *   pipe_notifier_drain()
 *
 * AgentIdentity - Thread-safe agent identity management
 *   agent_identity_create()
 *   agent_identity_destroy()
 *   agent_identity_get_id()
 *   agent_identity_get_parent_id()
 *   agent_identity_is_subagent()
 *
 * MessageStore - SQLite-backed messaging (direct + pub/sub)
 *   message_store_get_instance()
 *   message_store_create()
 *   message_store_destroy()
 *   message_send_direct()
 *   message_has_pending()
 */

#include "ipc/ipc.h"

/* =============================================================================
 * UI MODULE
 *
 * The UI module provides terminal output and display components.
 * Include lib/ui/ui.h for full access.
 * ============================================================================= */

/*
 * UI components are implemented in lib/ui/.
 * For full documentation, see lib/ui/ui.h and the individual headers.
 *
 * Quick reference:
 *
 * Output - Terminal formatting with colors and sections
 *   set_json_output_mode()
 *   display_streaming_init()
 *   display_streaming_text()
 *   display_streaming_complete()
 *   log_tool_execution_improved()
 *   cleanup_output_formatter()
 *
 * Spinner - Progress animation
 *   spinner_start()
 *   spinner_stop()
 *   spinner_cleanup()
 *
 * JSON Output - Machine-readable output mode
 *   json_output_init()
 *   json_output_assistant_text()
 *   json_output_tool_result()
 *   json_output_error()
 */

#include "ui/ui.h"

/* =============================================================================
 * TOOLS MODULE
 *
 * The Tools module provides the tool registration and execution framework.
 * Include lib/tools/tools.h for full access.
 * ============================================================================= */

/*
 * Tool components are implemented in lib/tools/.
 * For full documentation, see lib/tools/tools.h.
 *
 * Quick reference:
 *
 * Tool Registry - Container for available tools
 *   init_tool_registry()       - Initialize a registry
 *   cleanup_tool_registry()    - Destroy a registry
 *   register_tool()            - Register a custom tool
 *   execute_tool_call()        - Execute a tool call
 *
 * JSON Generation - For LLM API requests
 *   generate_tools_json()             - OpenAI format
 *   generate_anthropic_tools_json()   - Anthropic format
 *
 * Tool Call Parsing - From LLM responses
 *   parse_tool_calls()           - Parse OpenAI format
 *   parse_anthropic_tool_calls() - Parse Anthropic format
 */

#include "tools/tools.h"

#include "agent/agent.h"

/* =============================================================================
 * WORKFLOW MODULE
 *
 * The Workflow module provides task queue and worker management for
 * multi-agent orchestration scenarios.
 * ============================================================================= */

/*
 * Workflow components enable orchestrator agents to distribute work:
 *
 * Quick reference:
 *
 * WorkQueue - Named queue for distributing tasks
 *   work_queue_create()     - Create or get a queue by name
 *   work_queue_enqueue()    - Add a task to the queue
 *   work_queue_claim()      - Worker claims next available task
 *   work_queue_complete()   - Mark task as completed
 *   work_queue_fail()       - Mark task as failed (may retry)
 *   work_queue_destroy()    - Clean up queue handle
 *
 * WorkerHandle - Manage spawned worker agents
 *   worker_spawn()          - Spawn a worker for a queue
 *   worker_is_running()     - Check if worker is alive
 *   worker_stop()           - Terminate a worker
 */

#include "workflow/workflow.h"

/* Note: UI types (OutputConfig, ReplConfig, ReplCallbacks) are defined
 * in lib/ui/ui.h and lib/ui/repl.h. Include those headers for the actual API.
 *
 * The REPL is run via repl_run_session() which takes an AgentSession*.
 * See lib/ui/repl.h for the full interface.
 */

#ifdef __cplusplus
}
#endif

#endif /* LIBAGENT_H */
