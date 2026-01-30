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
 * This allows different binaries (ralph, ralph-orchestrator, workers)
 * to be thin wrappers that configure and invoke the same library.
 */

#ifndef LIB_AGENT_AGENT_H
#define LIB_AGENT_AGENT_H

#include <stdbool.h>
#include <stddef.h>

/* Re-export the core session types from src/ */
#include "../../src/core/ralph.h"
#include "../ipc/ipc.h"
#include "../tools/tools.h"
#include "../services/services.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * AGENT MODE
 * ============================================================================= */

/**
 * Agent execution mode determines how the agent interacts.
 */
typedef enum {
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
typedef struct RalphAgentConfig {
    /** Ralph home directory (uses default if NULL) */
    const char* home_dir;

    /** Execution mode */
    RalphAgentMode mode;

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

    /** CLI allowlist entries (tool:pattern format) */
    const char** allow_entries;
    int allow_entry_count;

    /** CLI allow categories */
    const char** allow_categories;
    int allow_category_count;

    /** Injected services (NULL uses default singletons) */
    RalphServices* services;
} RalphAgentConfig;

/* =============================================================================
 * AGENT STRUCTURE
 * ============================================================================= */

/**
 * The Agent wraps a RalphSession with a cleaner lifecycle API.
 */
typedef struct RalphAgent {
    RalphSession session;
    RalphAgentConfig config;
    RalphServices* services;      /**< Service container (owned if created internally) */
    bool owns_services;           /**< True if agent should destroy services on cleanup */
    bool initialized;
    bool config_loaded;
} RalphAgent;

/* =============================================================================
 * AGENT LIFECYCLE
 * ============================================================================= */

/**
 * Create a default agent configuration.
 *
 * @return Configuration with default values
 */
static inline RalphAgentConfig ralph_agent_config_default(void) {
    RalphAgentConfig config = {0};
    config.mode = RALPH_AGENT_MODE_INTERACTIVE;
    config.debug = false;
    config.json_mode = false;
    config.no_stream = false;
    config.yolo = false;
    config.no_auto_messages = false;
    config.message_poll_interval_ms = 0;  /* Use default */
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
int ralph_agent_init(RalphAgent* agent, const RalphAgentConfig* config);

/**
 * Load configuration for the agent.
 * Must be called after ralph_agent_init and before running.
 *
 * @param agent The agent
 * @return 0 on success, -1 on failure
 */
int ralph_agent_load_config(RalphAgent* agent);

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
int ralph_agent_run(RalphAgent* agent);

/**
 * Process a single message with an agent.
 * For programmatic use or SINGLE_SHOT mode.
 *
 * @param agent The agent
 * @param message User message to process
 * @return 0 on success, non-zero on error
 */
int ralph_agent_process_message(RalphAgent* agent, const char* message);

/**
 * Cleanup an agent and free all resources.
 *
 * @param agent Agent to cleanup
 */
void ralph_agent_cleanup(RalphAgent* agent);

/**
 * Get the underlying session from an agent.
 * Use for advanced access to session data.
 *
 * @param agent The agent
 * @return Pointer to session, or NULL if not initialized
 */
static inline RalphSession* ralph_agent_get_session(RalphAgent* agent) {
    return (agent != NULL && agent->initialized) ? &agent->session : NULL;
}

/**
 * Get the agent's session ID.
 *
 * @param agent The agent
 * @return Session ID string, or NULL if not initialized
 */
static inline const char* ralph_agent_get_session_id(RalphAgent* agent) {
    return (agent != NULL && agent->initialized) ? agent->session.session_id : NULL;
}

/**
 * Get the agent's services container.
 *
 * @param agent The agent
 * @return Services container, or NULL if not initialized
 */
static inline RalphServices* ralph_agent_get_services(RalphAgent* agent) {
    return (agent != NULL) ? agent->services : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* LIB_AGENT_AGENT_H */
