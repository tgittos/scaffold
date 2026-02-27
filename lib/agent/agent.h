/**
 * lib/agent/agent.h - Agent Abstraction
 *
 * This header provides the core agent abstraction that ties together
 * tools, session management, IPC, and UI components into a unified
 * agent lifecycle API.
 *
 * An agent is configured by:
 * - Mode: Interactive, single-shot, or background
 * - Tools: What capabilities are available
 * - System prompt: Defines the agent's role/behavior
 * - Services: Injected dependencies (message store, vector DB, etc.)
 *
 * This allows different binaries to be thin wrappers that configure
 * and invoke the same library.
 */

#ifndef LIB_AGENT_AGENT_H
#define LIB_AGENT_AGENT_H

#include <stdbool.h>
#include <stddef.h>

/* Session types from lib/ */
#include "session.h"
#include "../ipc/ipc.h"
#include "../tools/tools.h"
#include "../services/services.h"
#include "../orchestrator/supervisor.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * Agent execution mode determines how the agent interacts.
 */
typedef enum {
    /** Interactive REPL with a user */
    AGENT_MODE_INTERACTIVE = 0,

    /** Process a single message and exit */
    AGENT_MODE_SINGLE_SHOT = 1,

    /** Background agent, no TTY, communicates via messages */
    AGENT_MODE_BACKGROUND = 2,

    /** Worker mode: claims and processes items from a work queue */
    AGENT_MODE_WORKER = 3,

    /** Supervisor mode: drives a goal to completion via GOAP tools */
    AGENT_MODE_SUPERVISOR = 4
} AgentMode;

/**
 * Configuration for creating an agent.
 * All string fields are copied; caller retains ownership of originals.
 */
typedef struct AgentConfig {
    /** Application name (e.g. "scaffold") — sets home dir default */
    const char* app_name;

    /** Home directory override (uses default if NULL) */
    const char* home_dir;

    /** Execution mode */
    AgentMode mode;

    /** Enable debug output */
    bool debug;

    /** Enable JSON output mode */
    bool json_mode;

    /** Disable response streaming */
    bool no_stream;

    /** Disable all approval gates (yolo mode) */
    bool yolo;

    /** Disable automatic message polling */
    bool no_auto_messages;

    /** Message poll interval in milliseconds (0 for default) */
    int message_poll_interval_ms;

    /** Initial user message for SINGLE_SHOT mode (ignored otherwise) */
    const char* initial_message;

    /** Task description for BACKGROUND mode subagent */
    const char* subagent_task;

    /** Context for BACKGROUND mode subagent */
    const char* subagent_context;

    /** Queue name for WORKER mode */
    const char* worker_queue_name;

    /** System prompt for WORKER mode (NULL uses default) */
    const char* worker_system_prompt;

    /** Goal ID for SUPERVISOR mode */
    const char* supervisor_goal_id;

    /** Supervisor phase override (-1 = auto-detect from goal status) */
    int supervisor_phase;

    /** Model override (tier name or raw model ID) from --model flag */
    const char* model_override;

    /** CLI allowlist entries (tool:pattern format) */
    const char** allow_entries;
    int allow_entry_count;

    /** CLI allow categories */
    const char** allow_categories;
    int allow_category_count;

    /** Injected services (NULL uses default singletons) */
    Services* services;
} AgentConfig;

/**
 * The Agent wraps an AgentSession with a cleaner lifecycle API.
 */
typedef struct Agent {
    AgentSession session;
    AgentConfig config;
    Services* services;      /**< Service container (owned if created internally) */
    bool owns_services;           /**< True if agent should destroy services on cleanup */
    bool initialized;
    bool config_loaded;
} Agent;

/**
 * Create a default agent configuration.
 *
 * @return Configuration with default values
 */
static inline AgentConfig agent_config_default(void) {
    AgentConfig config = {0};
    config.mode = AGENT_MODE_INTERACTIVE;
    config.debug = false;
    config.json_mode = false;
    config.no_stream = false;
    config.yolo = false;
    config.no_auto_messages = false;
    config.message_poll_interval_ms = 0;  /* Use default */
    config.supervisor_phase = -1;         /* Auto-detect from goal status */
    config.services = NULL;               /* Use default singletons */
    return config;
}

/**
 * Initialize an agent with the given configuration.
 *
 * @param agent Agent structure to initialize
 * @param config Configuration (NULL uses defaults)
 * @return 0 on success, -1 on failure
 */
int agent_init(Agent* agent, const AgentConfig* config);

/**
 * Load configuration for the agent.
 * Must be called after agent_init and before running.
 *
 * @param agent The agent
 * @return 0 on success, -1 on failure
 */
int agent_load_config(Agent* agent);

/**
 * Run an agent based on its configured mode.
 *
 * For INTERACTIVE mode: Enters the REPL loop and blocks until user exits.
 * For SINGLE_SHOT mode: Processes config.initial_message and returns.
 * For BACKGROUND mode: Runs as subagent with config.subagent_task.
 *
 * @param agent The agent to run
 * @return 0 on success, non-zero on error
 */
int agent_run(Agent* agent);

/**
 * Process a single message with an agent.
 * For programmatic use or SINGLE_SHOT mode.
 *
 * @param agent The agent
 * @param message User message to process
 * @return 0 on success, non-zero on error
 */
int agent_process_message(Agent* agent, const char* message);

/**
 * Cleanup an agent and free all resources.
 *
 * @param agent Agent to cleanup
 */
void agent_cleanup(Agent* agent);

/**
 * Get the underlying session from an agent.
 * Use for advanced access to session data.
 *
 * @param agent The agent
 * @return Pointer to session, or NULL if not initialized
 */
static inline AgentSession* agent_get_session(Agent* agent) {
    return (agent != NULL && agent->initialized) ? &agent->session : NULL;
}

/**
 * Get the agent's session ID.
 *
 * @param agent The agent
 * @return Session ID string, or NULL if not initialized
 */
static inline const char* agent_get_session_id(Agent* agent) {
    return (agent != NULL && agent->initialized) ? agent->session.session_id : NULL;
}

/**
 * Get the agent's services container.
 *
 * @param agent The agent
 * @return Services container, or NULL if not initialized
 */
static inline Services* agent_get_services(Agent* agent) {
    return (agent != NULL) ? agent->services : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* LIB_AGENT_AGENT_H */
