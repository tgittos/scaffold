/**
 * lib/agent/agent.c - Agent Abstraction Implementation
 *
 * Implements the agent lifecycle API that wraps RalphSession.
 */

#include "agent.h"
#include "../ui/repl.h"
#include "../workflow/workflow.h"
#include "../../src/session/conversation_tracker.h"
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
#include <signal.h>
#include <unistd.h>
#include <readline/history.h>

/* Signal handler for worker graceful shutdown */
static volatile sig_atomic_t g_worker_running = 1;

static void worker_signal_handler(int signum) {
    (void)signum;
    g_worker_running = 0;
}

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

    /* For BACKGROUND mode (subagent), set env var BEFORE session init
     * so tool registration knows to skip subagent tools */
    if (agent->config.mode == RALPH_AGENT_MODE_BACKGROUND) {
        setenv("RALPH_IS_SUBAGENT", "1", 1);
    }

    /* Initialize session */
    if (ralph_init_session(&agent->session) != 0) {
        return -1;
    }

    /* Mark as subagent process after session init */
    if (agent->config.mode == RALPH_AGENT_MODE_BACKGROUND) {
        agent->session.subagent_manager.is_subagent_process = 1;
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

        case RALPH_AGENT_MODE_BACKGROUND: {
            if (agent->config.subagent_task == NULL) {
                return -1;
            }

            /* Initialize approval channel for IPC with parent */
            subagent_init_approval_channel();

            /* Subagents run with fresh context, not parent conversation history */
            cleanup_conversation_history(&agent->session.session_data.conversation);
            init_conversation_history(&agent->session.session_data.conversation);

            /* Build message with optional context */
            char* message = NULL;
            const char* task = agent->config.subagent_task;
            const char* context = agent->config.subagent_context;

            if (context != NULL && strlen(context) > 0) {
                size_t len = strlen("Context: ") + strlen(context) +
                             strlen("\n\nTask: ") + strlen(task) + 1;
                message = malloc(len);
                if (message != NULL) {
                    snprintf(message, len, "Context: %s\n\nTask: %s", context, task);
                }
            } else {
                message = strdup(task);
            }

            if (message == NULL) {
                subagent_cleanup_approval_channel();
                return -1;
            }

            /* Process the task */
            int result = ralph_process_message(&agent->session, message);

            free(message);
            subagent_cleanup_approval_channel();
            return result;
        }

        case RALPH_AGENT_MODE_WORKER: {
            if (agent->config.worker_queue_name == NULL) {
                fprintf(stderr, "Error: Worker mode requires queue name\n");
                return -1;
            }

            /* Set up signal handlers for graceful shutdown */
            struct sigaction sa;
            sa.sa_handler = worker_signal_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGTERM, &sa, NULL);
            sigaction(SIGINT, &sa, NULL);

            /* Open the work queue */
            WorkQueue* queue = work_queue_create(agent->config.worker_queue_name);
            if (queue == NULL) {
                fprintf(stderr, "Error: Failed to open queue '%s'\n",
                        agent->config.worker_queue_name);
                return -1;
            }

            debug_printf("Worker started for queue '%s'\n", agent->config.worker_queue_name);

            int items_processed = 0;
            int errors = 0;

            /* Worker loop */
            while (g_worker_running) {
                /* Claim next work item */
                WorkItem* item = work_queue_claim(queue, agent->session.session_id);
                if (item == NULL) {
                    /* Queue empty, wait briefly then check again */
                    usleep(1000000);  /* 1 second */
                    continue;
                }

                debug_printf("Worker claimed item %s: %s\n", item->id,
                             item->task_description ? item->task_description : "(no description)");

                /* Build the message to process */
                char* message = NULL;
                if (item->context != NULL && strlen(item->context) > 0) {
                    size_t len = strlen(item->task_description) + strlen(item->context) + 32;
                    message = malloc(len);
                    if (message != NULL) {
                        snprintf(message, len, "Context: %s\n\nTask: %s",
                                 item->context, item->task_description);
                    }
                } else {
                    message = strdup(item->task_description);
                }

                if (message == NULL) {
                    work_queue_fail(queue, item->id, "Failed to allocate message");
                    work_item_free(item);
                    errors++;
                    continue;
                }

                /* Reset conversation for fresh context per task */
                cleanup_conversation_history(&agent->session.session_data.conversation);
                init_conversation_history(&agent->session.session_data.conversation);

                /* Process the work item */
                int result = ralph_process_message(&agent->session, message);
                free(message);

                if (result == 0) {
                    work_queue_complete(queue, item->id, "Task completed successfully");
                    items_processed++;
                    debug_printf("Worker completed item %s\n", item->id);
                } else {
                    char error_msg[128];
                    snprintf(error_msg, sizeof(error_msg),
                             "Task processing failed with code %d", result);
                    work_queue_fail(queue, item->id, error_msg);
                    errors++;
                    debug_printf("Worker failed item %s: %s\n", item->id, error_msg);
                }

                work_item_free(item);
            }

            debug_printf("Worker shutting down: %d items processed, %d errors\n",
                         items_processed, errors);

            work_queue_destroy(queue);
            return (errors > 0) ? -1 : 0;
        }

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
