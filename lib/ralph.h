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
 * AGENT MODE
 * ============================================================================= */

/**
 * Agent execution mode determines how the agent interacts.
 */
typedef enum RalphAgentMode {
    /** Interactive REPL with a user */
    RALPH_AGENT_MODE_INTERACTIVE = 0,

    /** Process a single message and exit */
    RALPH_AGENT_MODE_SINGLE_SHOT = 1,

    /** Background agent, no TTY, communicates via messages */
    RALPH_AGENT_MODE_BACKGROUND = 2
} RalphAgentMode;

/* =============================================================================
 * AGENT CONFIGURATION
 * ============================================================================= */

/**
 * Configuration for creating an agent.
 * All string fields are copied; caller retains ownership of originals.
 */
struct RalphAgentConfig {
    /** Unique session identifier (generated if NULL) */
    const char* session_id;

    /** Ralph home directory (uses default if NULL) */
    const char* home_dir;

    /** System prompt defining agent role/behavior */
    const char* system_prompt;

    /** Execution mode */
    RalphAgentMode mode;

    /** Tool registry (NULL uses default tools for mode) */
    ToolRegistry* tools;

    /** Services (NULL uses default singleton services) */
    RalphServices* services;

    /** Parent agent identity for subagents (NULL if root) */
    AgentIdentity* parent_identity;

    /** Initial user message for SINGLE_SHOT mode (ignored otherwise) */
    const char* initial_message;

    /** Enable debug output */
    bool debug;

    /** Enable JSON output mode */
    bool json_mode;

    /** Disable all approval gates (yolo mode) */
    bool yolo;
};

/* =============================================================================
 * AGENT LIFECYCLE
 * ============================================================================= */

/**
 * Create a new agent with the given configuration.
 *
 * @param config Agent configuration (NULL uses defaults)
 * @return New agent instance, or NULL on failure. Caller must free with ralph_agent_destroy.
 */
RalphAgent* ralph_agent_create(const RalphAgentConfig* config);

/**
 * Run an agent in INTERACTIVE mode.
 * Enters the REPL loop and blocks until the user exits.
 *
 * @param agent The agent to run
 * @return 0 on success, non-zero on error
 */
int ralph_agent_run(RalphAgent* agent);

/**
 * Process a single message with an agent.
 * For SINGLE_SHOT or programmatic use.
 *
 * @param agent The agent
 * @param message User message to process
 * @param response Output: agent response (caller must free)
 * @return 0 on success, non-zero on error
 */
int ralph_agent_process(RalphAgent* agent, const char* message, char** response);

/**
 * Destroy an agent and free all resources.
 *
 * @param agent Agent to destroy (may be NULL)
 */
void ralph_agent_destroy(RalphAgent* agent);

/**
 * Get the agent's identity.
 *
 * @param agent The agent
 * @return Agent identity, or NULL on error. Do not free; owned by agent.
 */
AgentIdentity* ralph_agent_get_identity(RalphAgent* agent);

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

/* =============================================================================
 * AGENT MODULE
 *
 * The Agent module provides the core agent abstraction that ties together
 * tools, session management, IPC, and UI components.
 * ============================================================================= */

/*
 * The agent abstraction wraps RalphSession with a cleaner lifecycle API.
 * For full documentation, see lib/agent/agent.h.
 *
 * Quick reference:
 *
 * Agent Lifecycle:
 *   ralph_agent_init()              - Initialize agent with config
 *   ralph_agent_load_config()       - Load configuration
 *   ralph_agent_run()               - Run based on mode
 *   ralph_agent_process_message()   - Process a single message
 *   ralph_agent_cleanup()           - Cleanup and free resources
 *
 * Agent Modes:
 *   RALPH_AGENT_MODE_INTERACTIVE    - REPL with user
 *   RALPH_AGENT_MODE_SINGLE_SHOT    - Process one message, exit
 *   RALPH_AGENT_MODE_BACKGROUND     - Subagent, no TTY
 *
 * Helper Functions:
 *   ralph_agent_config_default()    - Get default config
 *   ralph_agent_get_session()       - Access underlying session
 *   ralph_agent_get_session_id()    - Get session ID
 */

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

/* =============================================================================
 * UI: OUTPUT FORMATTING
 * ============================================================================= */

/**
 * Output configuration for formatting.
 */
struct OutputConfig {
    bool json_mode;      /**< Output JSON instead of formatted text */
    bool color_enabled;  /**< Enable ANSI color codes */
    bool spinner_enabled;/**< Enable progress spinners */
};

/**
 * Initialize output with configuration.
 *
 * @param config Output configuration
 */
void ralph_output_init(const OutputConfig* config);

/**
 * Print formatted assistant message.
 *
 * @param message Message to print
 */
void ralph_output_assistant(const char* message);

/**
 * Print formatted error message.
 *
 * @param message Error message
 */
void ralph_output_error(const char* message);

/**
 * Print formatted info message.
 *
 * @param message Info message
 */
void ralph_output_info(const char* message);

/* =============================================================================
 * UI: REPL
 * ============================================================================= */

/**
 * REPL event callbacks.
 */
typedef struct ReplCallbacks {
    /** Called when user submits input. Return 0 to continue, non-zero to exit. */
    int (*on_input)(void* context, const char* input);

    /** Called when REPL is ready for input */
    void (*on_ready)(void* context);

    /** Called before REPL exits */
    void (*on_shutdown)(void* context);

    /** Called when external event is received on notifier pipe */
    void (*on_event)(void* context, char event);
} ReplCallbacks;

/**
 * REPL configuration.
 */
struct ReplConfig {
    ReplCallbacks callbacks;     /**< Event callbacks */
    void* context;               /**< User context passed to callbacks */
    PipeNotifier* event_notifier;/**< For async events (may be NULL) */
    const char* prompt;          /**< Input prompt string */
    const char* history_file;    /**< History file path (may be NULL) */
};

/**
 * Run the REPL with the given configuration.
 * Blocks until on_input returns non-zero or EOF.
 *
 * @param config REPL configuration
 * @return Exit code from on_input callback
 */
int ralph_repl_run(const ReplConfig* config);

/* =============================================================================
 * INITIALIZATION
 * ============================================================================= */

/**
 * Initialize libralph.
 * Must be called before any other libralph functions.
 *
 * @return 0 on success, -1 on failure
 */
int ralph_init(void);

/**
 * Shutdown libralph and release global resources.
 */
void ralph_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBRALPH_H */
