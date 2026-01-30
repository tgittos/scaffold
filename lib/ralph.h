/**
 * ralph.h - Public API for libralph
 *
 * libralph is the core library providing agent creation, configuration,
 * and execution. The ralph CLI binary is a thin wrapper around this library.
 *
 * This header defines the public API for embedding ralph functionality
 * in other programs or creating custom agent binaries.
 */

#ifndef LIBRALPH_H
#define LIBRALPH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * VERSION
 * ============================================================================= */

#define LIBRALPH_VERSION_MAJOR 0
#define LIBRALPH_VERSION_MINOR 1
#define LIBRALPH_VERSION_PATCH 0

/* =============================================================================
 * FORWARD DECLARATIONS
 * ============================================================================= */

/* Core agent types */
typedef struct RalphAgent RalphAgent;
typedef struct RalphAgentConfig RalphAgentConfig;
typedef struct RalphServices RalphServices;

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
 *   RalphAgentMode   - INTERACTIVE, SINGLE_SHOT, or BACKGROUND
 *   RalphAgentConfig - Configuration struct
 *   RalphAgent       - Agent instance
 *
 * Key functions:
 *   ralph_agent_init()            - Initialize agent
 *   ralph_agent_load_config()     - Load configuration
 *   ralph_agent_run()             - Run based on mode
 *   ralph_agent_process_message() - Process single message
 *   ralph_agent_cleanup()         - Free resources
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
 * Include lib/ipc/ipc.h for full access, or use the convenience macros below.
 * ============================================================================= */

/*
 * IPC components are implemented in src/ and re-exported through lib/ipc/.
 * For full documentation, see lib/ipc/ipc.h and the individual headers.
 *
 * Quick reference:
 *
 * PipeNotifier - Non-blocking pipe for async event notification
 *   ralph_pipe_notifier_init()
 *   ralph_pipe_notifier_destroy()
 *   ralph_pipe_notifier_send()
 *   ralph_pipe_notifier_recv()
 *   ralph_pipe_notifier_get_read_fd()
 *   ralph_pipe_notifier_drain()
 *
 * AgentIdentity - Thread-safe agent identity management
 *   ralph_agent_identity_create()
 *   ralph_agent_identity_destroy()
 *   ralph_agent_identity_get_id()
 *   ralph_agent_identity_get_parent_id()
 *   ralph_agent_identity_is_subagent()
 *
 * MessageStore - SQLite-backed messaging (direct + pub/sub)
 *   ralph_message_store_get()
 *   ralph_message_store_create()
 *   ralph_message_store_destroy()
 *   ralph_message_send_direct()
 *   ralph_message_has_pending()
 */

#include "ipc/ipc.h"

/* =============================================================================
 * UI MODULE
 *
 * The UI module provides terminal output and display components.
 * Include lib/ui/ui.h for full access, or use the convenience macros below.
 * ============================================================================= */

/*
 * UI components are implemented in src/ and re-exported through lib/ui/.
 * For full documentation, see lib/ui/ui.h and the individual headers.
 *
 * Quick reference:
 *
 * Output - Terminal formatting with colors and sections
 *   ralph_output_set_json_mode()
 *   ralph_output_streaming_init()
 *   ralph_output_streaming_text()
 *   ralph_output_streaming_complete()
 *   ralph_output_tool_execution()
 *   ralph_output_cleanup()
 *
 * Spinner - Progress animation
 *   ralph_spinner_start()
 *   ralph_spinner_stop()
 *   ralph_spinner_cleanup()
 *
 * JSON Output - Machine-readable output mode
 *   ralph_json_output_init()
 *   ralph_json_output_assistant_text()
 *   ralph_json_output_tool_result()
 *   ralph_json_output_error()
 */

#include "ui/ui.h"

/* =============================================================================
 * TOOLS MODULE
 *
 * The Tools module provides the tool registration and execution framework.
 * Include lib/tools/tools.h for full access, or use the convenience macros below.
 * ============================================================================= */

/*
 * Tool components are implemented in src/ and re-exported through lib/tools/.
 * For full documentation, see lib/tools/tools.h.
 *
 * Quick reference:
 *
 * Tool Registry - Container for available tools
 *   ralph_tools_create_cli()        - Create registry with CLI tools
 *   ralph_tools_create_empty()      - Create empty registry
 *   ralph_tools_destroy()           - Destroy a registry
 *   ralph_tools_register()          - Register a custom tool
 *   ralph_tools_execute()           - Execute a tool call
 *
 * JSON Generation - For LLM API requests
 *   ralph_tools_generate_json()     - OpenAI format
 *   ralph_tools_generate_anthropic_json() - Anthropic format
 *
 * Tool Call Parsing - From LLM responses
 *   ralph_tools_parse_calls()       - Parse OpenAI format
 *   ralph_tools_parse_anthropic()   - Parse Anthropic format
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
 * The REPL is run via ralph_repl_run_session() which takes a RalphSession*.
 * See lib/ui/repl.h for the full interface.
 */

#ifdef __cplusplus
}
#endif

#endif /* LIBRALPH_H */
