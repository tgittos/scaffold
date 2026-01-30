/**
 * lib/agent/agent.c - Agent Abstraction Implementation
 *
 * Implements the agent lifecycle API that wraps RalphSession.
 */

#include "agent.h"
#include "../ui/repl.h"
#include "../../src/utils/debug_output.h"
#include "../../src/utils/output_formatter.h"
#include "../../src/utils/json_output.h"
#include "../../src/utils/ralph_home.h"
#include "../../src/utils/spinner.h"
#include "../../src/policy/approval_gate.h"
#include "../../src/tools/subagent_tool.h"
#include "../../src/cli/memory_commands.h"
#include "../../src/messaging/message_poller.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <readline/history.h>

int ralph_agent_init(RalphAgent* agent, const RalphAgentConfig* config) {
    if (agent == NULL) {
        return -1;
    }

    memset(agent, 0, sizeof(RalphAgent));

    /* Copy configuration */
    if (config != NULL) {
        agent->config = *config;
    } else {
        agent->config = ralph_agent_config_default();
    }

    /* Initialize ralph home directory FIRST - services depend on it */
    if (ralph_home_init(agent->config.home_dir) != 0) {
        return -1;
    }

    /* Set up services container (after ralph_home is initialized) */
    if (agent->config.services != NULL) {
        /* Use provided services */
        agent->services = agent->config.services;
        agent->owns_services = false;
    } else {
        /* Create default services using singletons */
        agent->services = ralph_services_create_default();
        agent->owns_services = true;
    }

    /* Initialize debug mode */
    debug_init(agent->config.debug);

    /* Initialize session */
    if (ralph_init_session(&agent->session) != 0) {
        return -1;
    }

    agent->initialized = true;

    /* Apply polling configuration */
    if (agent->config.no_auto_messages) {
        agent->session.polling_config.auto_poll_enabled = 0;
    }
    if (agent->config.message_poll_interval_ms > 0) {
        agent->session.polling_config.poll_interval_ms = agent->config.message_poll_interval_ms;
    }

    return 0;
}

int ralph_agent_load_config(RalphAgent* agent) {
    if (agent == NULL || !agent->initialized) {
        return -1;
    }

    if (ralph_load_config(&agent->session) != 0) {
        return -1;
    }

    /* Apply CLI overrides for approval gate */
    if (agent->config.yolo) {
        approval_gate_enable_yolo(&agent->session.gate_config);
        debug_printf("Approval gates disabled (yolo mode)\n");
    }

    /* Apply category allowances */
    for (int i = 0; i < agent->config.allow_category_count; i++) {
        if (approval_gate_set_category_action(&agent->session.gate_config,
                                              agent->config.allow_categories[i],
                                              GATE_ACTION_ALLOW) != 0) {
            debug_printf("Warning: Unknown category '%s'\n", agent->config.allow_categories[i]);
        }
    }

    /* Apply allow entries */
    for (int i = 0; i < agent->config.allow_entry_count; i++) {
        if (approval_gate_add_cli_allow(&agent->session.gate_config,
                                        agent->config.allow_entries[i]) != 0) {
            debug_printf("Warning: Invalid allow entry '%s'\n", agent->config.allow_entries[i]);
        }
    }

    /* Apply streaming setting */
    if (agent->config.no_stream) {
        agent->session.session_data.config.enable_streaming = false;
    }

    /* Apply JSON mode */
    if (agent->config.json_mode) {
        agent->session.session_data.config.json_output_mode = true;
        set_json_output_mode(true);
        json_output_init();
    }

    agent->config_loaded = true;
    return 0;
}

int ralph_agent_run(RalphAgent* agent) {
    if (agent == NULL || !agent->initialized || !agent->config_loaded) {
        return -1;
    }

    switch (agent->config.mode) {
        case RALPH_AGENT_MODE_SINGLE_SHOT:
            if (agent->config.initial_message == NULL) {
                return -1;
            }
            /* For single-shot, disable auto polling */
            agent->session.polling_config.auto_poll_enabled = 0;
            return ralph_process_message(&agent->session, agent->config.initial_message);

        case RALPH_AGENT_MODE_BACKGROUND:
            if (agent->config.subagent_task == NULL) {
                return -1;
            }
            return ralph_run_as_subagent(agent->config.subagent_task,
                                         agent->config.subagent_context);

        case RALPH_AGENT_MODE_INTERACTIVE:
        default: {
            /* Print banner */
            if (!agent->config.json_mode) {
                printf("\033[1mRalph\033[0m - AI Assistant\n");
                printf("Commands: quit, exit | Ctrl+D to end\n\n");
            }

            /* Show greeting or recap */
            ralph_repl_show_greeting(&agent->session, agent->config.json_mode);

            /* Initialize readline history and memory commands */
            using_history();
            memory_commands_init();

            /* Start message polling */
            ralph_start_message_polling(&agent->session);

            /* Run the REPL */
            int result = ralph_repl_run_session(&agent->session, agent->config.json_mode);

            /* Cleanup */
            ralph_stop_message_polling(&agent->session);
            memory_commands_cleanup();
            spinner_cleanup();

            return result;
        }
    }
}

int ralph_agent_process_message(RalphAgent* agent, const char* message) {
    if (agent == NULL || !agent->initialized || message == NULL) {
        return -1;
    }

    /* Ensure config is loaded */
    if (!agent->config_loaded) {
        if (ralph_agent_load_config(agent) != 0) {
            return -1;
        }
    }

    return ralph_process_message(&agent->session, message);
}

void ralph_agent_cleanup(RalphAgent* agent) {
    if (agent == NULL) {
        return;
    }

    if (agent->initialized) {
        ralph_cleanup_session(&agent->session);
        agent->initialized = false;
        agent->config_loaded = false;
    }

    /* Clean up services if we own them */
    if (agent->owns_services && agent->services != NULL) {
        ralph_services_destroy(agent->services);
        agent->services = NULL;
        agent->owns_services = false;
    }
}
