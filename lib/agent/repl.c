#include "repl.h"
#include "recap.h"
#include "../util/interrupt.h"
#include "../util/debug_output.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../ui/spinner.h"
#include "../ui/status_line.h"
#include "../ui/memory_commands.h"
#include "../tools/subagent_tool.h"
#include "../ipc/message_poller.h"
#include "../ipc/notification_formatter.h"
#include "../session/conversation_tracker.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <readline/readline.h>
#include <readline/history.h>

/* Global state for readline callbacks */
static volatile int g_repl_running = 1;
static AgentSession* g_repl_session = NULL;
static char* g_current_prompt = NULL;

static void repl_line_callback(char* line);

static char* repl_get_prompt(void) {
    char *p = status_line_build_prompt();
    return p ? p : strdup("> ");
}

static void repl_clear_prompt_area(void) {
    printf(TERM_CLEAR_LINE);
    printf("\033[A" TERM_CLEAR_LINE);
    status_line_clear_rendered();
    fflush(stdout);
}

static void repl_install_prompt(void) {
    status_line_render_info();
    printf("\n");
    free(g_current_prompt);
    g_current_prompt = repl_get_prompt();
    rl_callback_handler_install(g_current_prompt, repl_line_callback);
}

static void repl_update_agent_status(SubagentManager *mgr) {
    StatusAgentInfo infos[8];
    int n = 0;
    for (size_t i = 0; i < mgr->subagents.count && n < 8; i++) {
        Subagent *sub = &mgr->subagents.data[i];
        if (sub->status == SUBAGENT_STATUS_RUNNING) {
            infos[n].id = sub->id;
            infos[n].task = sub->task;
            infos[n].start_time = sub->start_time;
            n++;
        }
    }
    status_line_update_agents(n, infos);
}


static void repl_line_callback(char* line) {
    if (line == NULL) {
        printf("\n");
        g_repl_running = 0;
        rl_callback_handler_remove();
        return;
    }

    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) {
        printf("Goodbye!\n");
        free(line);
        g_repl_running = 0;
        rl_callback_handler_remove();
        return;
    }

    if (strlen(line) == 0) {
        free(line);
        return;
    }

    add_history(line);

    if (line[0] == '/') {
        if (process_memory_command(line) == 0) {
            free(line);
            return;
        }
    }

    printf("\n");
    int result = session_process_message(g_repl_session, line);
    if (result == -1) {
        fprintf(stderr, "Error: Failed to process message\n");
    }

    free(line);

    if (g_repl_running) {
        rl_callback_handler_remove();
        repl_install_prompt();
    }
}

static void process_pending_notifications(AgentSession* session) {
    notification_bundle_t* bundle = notification_bundle_create(session->session_id, session->services);
    if (bundle == NULL) {
        return;
    }
    int total_count = notification_bundle_total_count(bundle);
    if (total_count > 0) {
        int generic_count = 0;
        for (size_t i = 0; i < bundle->count; i++) {
            const char *content = bundle->messages[i].content;
            if (content && strstr(content, "subagent_completion")) {
                cJSON *root = cJSON_Parse(content);
                if (root) {
                    cJSON *type = cJSON_GetObjectItem(root, "type");
                    if (type && cJSON_IsString(type) &&
                        strcmp(type->valuestring, "subagent_completion") == 0) {
                        cJSON *task = cJSON_GetObjectItem(root, "task");
                        cJSON *status = cJSON_GetObjectItem(root, "status");
                        cJSON *elapsed = cJSON_GetObjectItem(root, "elapsed_seconds");
                        const char *task_str = (task && cJSON_IsString(task))
                                               ? task->valuestring : "unknown";
                        bool success = (status && cJSON_IsString(status) &&
                                       strcmp(status->valuestring, "completed") == 0);
                        int elapsed_secs = (elapsed && cJSON_IsNumber(elapsed))
                                           ? elapsed->valueint : 0;
                        display_agent_completed(task_str, elapsed_secs, success);
                        cJSON_Delete(root);
                        continue;
                    }
                    cJSON_Delete(root);
                }
            }
            generic_count++;
        }
        if (generic_count > 0) {
            display_message_notification(generic_count);
        }
        char* notification_text = notification_format_for_llm(bundle);
        if (notification_text != NULL) {
            debug_printf("Processing %d incoming messages\n", total_count);
            append_conversation_message(&session->session_data.conversation,
                                        "system", notification_text);
            session_continue(session);
            free(notification_text);
        } else {
            display_message_notification_clear();
        }
    }
    notification_bundle_destroy(bundle);
}

int repl_run_session(AgentSession* session, bool json_mode) {
    (void)json_mode;
    g_repl_session = session;
    g_repl_running = 1;

    if (interrupt_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize interrupt handling\n");
    }

    repl_install_prompt();

    int notify_fd = -1;
    if (session->message_poller != NULL) {
        notify_fd = message_poller_get_notify_fd(session->message_poller);
    }

    while (g_repl_running) {
        interrupt_clear();

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);

        int max_fd = STDIN_FILENO;

        if (notify_fd >= 0) {
            FD_SET(notify_fd, &read_fds);
            if (notify_fd > max_fd) {
                max_fd = notify_fd;
            }
        }

        SubagentManager* mgr = &session->subagent_manager;
        for (size_t i = 0; i < mgr->subagents.count; i++) {
            Subagent* sub = &mgr->subagents.data[i];
            if (sub->status == SUBAGENT_STATUS_RUNNING &&
                sub->approval_channel.request_fd > 2) {
                FD_SET(sub->approval_channel.request_fd, &read_fds);
                if (sub->approval_channel.request_fd > max_fd) {
                    max_fd = sub->approval_channel.request_fd;
                }
            }
        }

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready < 0) {
            if (interrupt_pending()) {
                interrupt_clear();
                rl_replace_line("", 0);
                rl_redisplay();
                continue;
            }
            if (g_repl_running) {
                continue;
            }
            break;
        }

        int subagent_changes = subagent_poll_all(mgr);
        if (subagent_changes > 0) {
            repl_update_agent_status(mgr);
            rl_callback_handler_remove();
            repl_clear_prompt_area();
            process_pending_notifications(session);
            if (g_repl_running) {
                repl_install_prompt();
            }
        }

        if (ready == 0) {
            continue;
        }

        for (size_t i = 0; i < mgr->subagents.count; i++) {
            Subagent* sub = &mgr->subagents.data[i];
            if (sub->status == SUBAGENT_STATUS_RUNNING &&
                sub->approval_channel.request_fd > 2 &&
                FD_ISSET(sub->approval_channel.request_fd, &read_fds)) {
                rl_callback_handler_remove();
                repl_clear_prompt_area();

                subagent_handle_approval_request(mgr, (int)i, &session->gate_config);

                if (g_repl_running) {
                    repl_install_prompt();
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            rl_callback_read_char();
        }

        if (notify_fd >= 0 && FD_ISSET(notify_fd, &read_fds)) {
            rl_callback_handler_remove();
            repl_clear_prompt_area();

            message_poller_clear_notification(session->message_poller);
            process_pending_notifications(session);

            if (g_repl_running) {
                repl_install_prompt();
            }
        }
    }

    rl_callback_handler_remove();
    free(g_current_prompt);
    g_current_prompt = NULL;
    status_line_cleanup();
    interrupt_cleanup();
    return 0;
}

void repl_show_greeting(AgentSession* session, bool json_mode) {
    if (json_mode) {
        return;
    }

    if (session->session_data.conversation.count == 0) {
        debug_printf("Generating welcome message...\n");

        const char* greeting_prompt = "This is your first interaction with this user in interactive mode. "
                                      "Please introduce yourself as Ralph, briefly explain your capabilities "
                                      "(answering questions, running shell commands, file operations, problem-solving), "
                                      "and ask what you can help with today. Keep it warm, concise, and engaging. "
                                      "Make it feel personal and conversational, not like a static template.";

        int result = session_process_message(session, greeting_prompt);
        if (result != 0) {
            printf("Hello! I'm Ralph, your AI assistant. What can I help you with today?\n");
        }
    } else {
        debug_printf("Generating recap of recent conversation (%d messages)...\n",
                     session->session_data.conversation.count);

        int result = session_generate_recap(session, 5);
        if (result != 0) {
            printf("Welcome back! Ready to continue where we left off.\n");
        }
    }
}
