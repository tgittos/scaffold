#include "repl.h"
#include "recap.h"
#include "../util/interrupt.h"
#include "../util/debug_output.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../ui/spinner.h"
#include "../ui/memory_commands.h"
#include "../tools/subagent_tool.h"
#include "../ipc/message_poller.h"
#include "../ipc/notification_formatter.h"
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
    // result == -2 (interrupted): cancellation message already shown by tool_executor
    printf("\n");

    free(line);
}

int repl_run_session(AgentSession* session, bool json_mode) {
    (void)json_mode;
    g_repl_session = session;
    g_repl_running = 1;

    if (interrupt_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize interrupt handling\n");
    }

    rl_callback_handler_install("> ", repl_line_callback);

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
                printf("\n");
                continue;
            }
            if (g_repl_running) {
                continue;
            }
            break;
        }

        if (ready == 0) {
            subagent_poll_all(mgr);
            continue;
        }

        for (size_t i = 0; i < mgr->subagents.count; i++) {
            Subagent* sub = &mgr->subagents.data[i];
            if (sub->status == SUBAGENT_STATUS_RUNNING &&
                sub->approval_channel.request_fd > 2 &&
                FD_ISSET(sub->approval_channel.request_fd, &read_fds)) {
                rl_callback_handler_remove();

                subagent_handle_approval_request(mgr, (int)i, &session->gate_config);

                if (g_repl_running) {
                    rl_callback_handler_install("> ", repl_line_callback);
                }
            }
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            rl_callback_read_char();
        }

        if (notify_fd >= 0 && FD_ISSET(notify_fd, &read_fds)) {
            rl_callback_handler_remove();
            printf(TERM_CLEAR_LINE);
            fflush(stdout);

            message_poller_clear_notification(session->message_poller);
            notification_bundle_t* bundle = notification_bundle_create(session->session_id, session->services);
            if (bundle != NULL) {
                int total_count = notification_bundle_total_count(bundle);
                if (total_count > 0) {
                    display_message_notification(total_count);
                    char* notification_text = notification_format_for_llm(bundle);
                    if (notification_text != NULL) {
                        debug_printf("Processing %d incoming messages\n", total_count);
                        session_process_message(session, notification_text);
                        free(notification_text);
                    } else {
                        display_message_notification_clear();
                    }
                }
                notification_bundle_destroy(bundle);
            }

            if (g_repl_running) {
                rl_callback_handler_install("> ", repl_line_callback);
            }
        }
    }

    rl_callback_handler_remove();
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
        printf("\n");
    } else {
        debug_printf("Generating recap of recent conversation (%d messages)...\n",
                     session->session_data.conversation.count);

        int result = session_generate_recap(session, 5);
        if (result != 0) {
            printf("Welcome back! Ready to continue where we left off.\n");
        }
        printf("\n");
    }
}
