#include "agent.h"
#include "repl.h"
#include "../workflow/workflow.h"
#include "../session/conversation_tracker.h"
#include "../util/debug_output.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../util/app_home.h"
#include "../ui/spinner.h"
#include "../ui/status_line.h"
#include "../policy/approval_gate.h"
#include "../tools/subagent_tool.h"
#include "../tools/tool_extension.h"
#include "../ui/slash_commands.h"
#include "../tools/memory_tool.h"
#include "../util/context_retriever.h"
#include "../ipc/message_poller.h"
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

int agent_init(Agent* agent, const AgentConfig* config) {
    if (agent == NULL) {
        return -1;
    }

    memset(agent, 0, sizeof(Agent));

    if (config != NULL) {
        agent->config = *config;
    } else {
        agent->config = agent_config_default();
    }

    /* Initialize home directory FIRST - services depend on it */
    /* Always set app name (NULL resets to default "ralph") */
    app_home_set_app_name(agent->config.app_name);
    if (app_home_init(agent->config.home_dir) != 0) {
        return -1;
    }

    if (agent->config.services != NULL) {
        agent->services = agent->config.services;
        agent->owns_services = false;
    } else {
        agent->services = services_create_default();
        if (agent->services == NULL) {
            return -1;
        }
        agent->owns_services = true;
    }

    debug_init(agent->config.debug);

    /* For BACKGROUND mode (subagent), set env var BEFORE session init
     * so tool registration knows to skip subagent tools */
    if (agent->config.mode == AGENT_MODE_BACKGROUND) {
        setenv("AGENT_IS_SUBAGENT", "1", 1);
    }

    /* conversation_tracker needs services before session_init because
     * load_conversation_history() is called during session_init. */
    conversation_tracker_set_services(agent->services);

    if (session_init(&agent->session) != 0) {
        return -1;
    }

    agent->session.model_override = agent->config.model_override;

    /* Wire remaining services after session_init — register_subagent_tool() copied
     * registry->services during session_init when it was still NULL. */
    agent->session.services = agent->services;
    agent->session.tools.services = agent->services;
    subagent_manager_set_services(&agent->session.subagent_manager, agent->services);
    document_store_set_services(agent->services);
    memory_tool_set_services(agent->services);
    context_retriever_set_services(agent->services);
    session_wire_services(&agent->session);

    /* Wire policy→tools callbacks so the policy layer never imports tools headers */
    ApprovalGateCallbacks gate_cbs = {
        .is_extension_tool = tool_extension_is_extension_tool,
        .get_gate_category = tool_extension_get_gate_category,
        .get_match_arg     = tool_extension_get_match_arg,
        .get_approval_channel = subagent_get_approval_channel,
        .log_approval      = log_subagent_approval,
    };
    approval_gate_set_callbacks(&gate_cbs);

    if (agent->config.mode == AGENT_MODE_BACKGROUND) {
        agent->session.subagent_manager.is_subagent_process = 1;
    }

    agent->initialized = true;

    if (agent->config.no_auto_messages) {
        agent->session.polling_config.auto_poll_enabled = 0;
    }
    if (agent->config.message_poll_interval_ms > 0) {
        agent->session.polling_config.poll_interval_ms = agent->config.message_poll_interval_ms;
    }

    return 0;
}

int agent_load_config(Agent* agent) {
    if (agent == NULL || !agent->initialized) {
        return -1;
    }

    if (session_load_config(&agent->session) != 0) {
        return -1;
    }

    if (agent->config.worker_system_prompt != NULL) {
        char *dup = strdup(agent->config.worker_system_prompt);
        if (dup == NULL) {
            return -1;
        }
        free(agent->session.session_data.config.system_prompt);
        agent->session.session_data.config.system_prompt = dup;
    }

    if (agent->config.yolo) {
        approval_gate_enable_yolo(&agent->session.gate_config);
        debug_printf("Approval gates disabled (yolo mode)\n");
    }

    for (int i = 0; i < agent->config.allow_category_count; i++) {
        if (approval_gate_set_category_action(&agent->session.gate_config,
                                              agent->config.allow_categories[i],
                                              GATE_ACTION_ALLOW) != 0) {
            debug_printf("Warning: Unknown category '%s'\n", agent->config.allow_categories[i]);
        }
    }

    for (int i = 0; i < agent->config.allow_entry_count; i++) {
        if (approval_gate_add_cli_allow(&agent->session.gate_config,
                                        agent->config.allow_entries[i]) != 0) {
            debug_printf("Warning: Invalid allow entry '%s'\n", agent->config.allow_entries[i]);
        }
    }

    if (agent->config.no_stream) {
        agent->session.session_data.config.enable_streaming = false;
    }

    if (agent->config.json_mode) {
        agent->session.session_data.config.json_output_mode = true;
        set_json_output_mode(true);
        json_output_init();
    }

    agent->config_loaded = true;
    return 0;
}

int agent_run(Agent* agent) {
    if (agent == NULL || !agent->initialized || !agent->config_loaded) {
        return -1;
    }

    switch (agent->config.mode) {
        case AGENT_MODE_SINGLE_SHOT:
            if (agent->config.initial_message == NULL) {
                return -1;
            }
            agent->session.polling_config.auto_poll_enabled = 0;
            return session_process_message(&agent->session, agent->config.initial_message);

        case AGENT_MODE_BACKGROUND: {
            if (agent->config.subagent_task == NULL) {
                return -1;
            }

            subagent_init_approval_channel();

            /* Subagents run with fresh context, not parent conversation history */
            cleanup_conversation_history(&agent->session.session_data.conversation);
            init_conversation_history(&agent->session.session_data.conversation);

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

            int result = session_process_message(&agent->session, message);

            free(message);
            subagent_cleanup_approval_channel();
            return result;
        }

        case AGENT_MODE_WORKER: {
            if (agent->config.worker_queue_name == NULL) {
                fprintf(stderr, "Error: Worker mode requires queue name\n");
                return -1;
            }

            struct sigaction sa;
            sa.sa_handler = worker_signal_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGTERM, &sa, NULL);
            sigaction(SIGINT, &sa, NULL);

            WorkQueue* queue = work_queue_create(agent->config.worker_queue_name);
            if (queue == NULL) {
                fprintf(stderr, "Error: Failed to open queue '%s'\n",
                        agent->config.worker_queue_name);
                return -1;
            }

            debug_printf("Worker started for queue '%s'\n", agent->config.worker_queue_name);

            int items_processed = 0;
            int errors = 0;

            while (g_worker_running) {
                WorkItem* item = work_queue_claim(queue, agent->session.session_id);
                if (item == NULL) {
                    break;
                }

                debug_printf("Worker claimed item %s: %s\n", item->id,
                             item->task_description ? item->task_description : "(no description)");

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

                cleanup_conversation_history(&agent->session.session_data.conversation);
                init_conversation_history(&agent->session.session_data.conversation);

                int result = session_process_message(&agent->session, message);
                free(message);

                if (result == 0) {
                    const char* worker_result = "Task completed successfully";
                    ConversationHistory* conv = &agent->session.session_data.conversation;
                    for (int ci = (int)conv->count - 1; ci >= 0; ci--) {
                        if (conv->data[ci].role &&
                            strcmp(conv->data[ci].role, "assistant") == 0 &&
                            conv->data[ci].content) {
                            worker_result = conv->data[ci].content;
                            break;
                        }
                    }
                    work_queue_complete(queue, item->id, worker_result);
                    items_processed++;
                    debug_printf("Worker completed item %s\n", item->id);
                } else if (result == -2) {
                    work_queue_complete(queue, item->id, "Task interrupted by user");
                    debug_printf("Worker item %s interrupted by user\n", item->id);
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

        case AGENT_MODE_INTERACTIVE:
        default: {
            if (!agent->config.json_mode) {
                printf(TERM_BOLD "Ralph" TERM_RESET " - AI Assistant\n");
                printf("Type /help for commands | quit, exit, Ctrl+D to end\n\n");
            }

            status_line_init();
            repl_show_greeting(&agent->session, agent->config.json_mode);

            using_history();
            slash_commands_init(&agent->session);

            session_start_message_polling(&agent->session);

            int result = repl_run_session(&agent->session, agent->config.json_mode);

            session_stop_message_polling(&agent->session);
            slash_commands_cleanup();
            spinner_cleanup();

            return result;
        }
    }
}

int agent_process_message(Agent* agent, const char* message) {
    if (agent == NULL || !agent->initialized || message == NULL) {
        return -1;
    }

    if (!agent->config_loaded) {
        if (agent_load_config(agent) != 0) {
            return -1;
        }
    }

    return session_process_message(&agent->session, message);
}

void agent_cleanup(Agent* agent) {
    if (agent == NULL) {
        return;
    }

    if (agent->initialized) {
        session_cleanup(&agent->session);
        agent->initialized = false;
        agent->config_loaded = false;
    }

    if (agent->owns_services && agent->services != NULL) {
        services_destroy(agent->services);
        agent->services = NULL;
        agent->owns_services = false;
    }

    cleanup_output_formatter();
}
