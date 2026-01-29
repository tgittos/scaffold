#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ralph.h"
#include "interrupt.h"
#include "debug_output.h"
#include "output_formatter.h"
#include "json_output.h"
#include "../policy/approval_gate.h"
#include "../cli/memory_commands.h"
#include "../tools/subagent_tool.h"
#include "../utils/ralph_home.h"

#define MAX_INPUT_SIZE 8192
#define RALPH_VERSION "0.1.0"
#define MAX_CLI_ALLOW_ENTRIES 64
#define MAX_CLI_ALLOW_CATEGORIES 16

static void print_version(void) {
    printf("ralph %s\n", RALPH_VERSION);
}

static void print_help(const char *program_name) {
    printf("ralph %s - AI Assistant\n\n", RALPH_VERSION);
    printf("Usage: %s [OPTIONS] [MESSAGE]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help                Show this help message and exit\n");
    printf("  -v, --version             Show version information and exit\n");
    printf("  --debug                   Enable debug output (shows HTTP requests and data exchange)\n");
    printf("  --no-stream               Disable response streaming\n");
    printf("  --json                    Enable JSON output mode\n");
    printf("  --home <path>             Override Ralph home directory (default: ~/.local/ralph)\n");
    printf("  --yolo                    Disable all approval gates for this session\n");
    printf("  --allow <spec>            Add allowlist entry (e.g., 'shell:git,status' or 'file_write:.*\\.txt$')\n");
    printf("  --allow-category=<cat>    Allow all operations in category (file_write, shell, network, etc.)\n");
    printf("\n");
    printf("Arguments:\n");
    printf("  MESSAGE           Process a single message and exit\n");
    printf("                    If omitted, enters interactive mode\n");
    printf("\n");
    printf("Interactive Mode Commands:\n");
    printf("  quit, exit        Exit the program\n");
    printf("  /memory           Memory management commands (use /memory help for details)\n");
    printf("  Ctrl+D            End session\n");
}

static void apply_gate_cli_overrides(RalphSession *session, bool yolo_mode,
                                     const char **cli_allow_categories, int cli_allow_category_count,
                                     const char **cli_allow_entries, int cli_allow_count) {
    if (yolo_mode) {
        approval_gate_enable_yolo(&session->gate_config);
        debug_printf("Approval gates disabled (--yolo mode)\n");
    }

    for (int i = 0; i < cli_allow_category_count; i++) {
        if (approval_gate_set_category_action(&session->gate_config, cli_allow_categories[i],
                                              GATE_ACTION_ALLOW) != 0) {
            fprintf(stderr, "Warning: Unknown category '%s' for --allow-category\n",
                    cli_allow_categories[i]);
        } else {
            debug_printf("Category '%s' set to allow via CLI\n", cli_allow_categories[i]);
        }
    }

    for (int i = 0; i < cli_allow_count; i++) {
        if (approval_gate_add_cli_allow(&session->gate_config, cli_allow_entries[i]) != 0) {
            fprintf(stderr, "Warning: Invalid format '%s' for --allow\n", cli_allow_entries[i]);
        } else {
            debug_printf("Added allow entry '%s' via CLI\n", cli_allow_entries[i]);
        }
    }
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            print_version();
            return EXIT_SUCCESS;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    // --home must be parsed before any initialization because other modules depend on ralph_home
    char *home_override = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            home_override = argv[++i];
            break;
        }
    }

    if (ralph_home_init(home_override) != 0) {
        fprintf(stderr, "Error: Failed to initialize ralph home directory\n");
        return EXIT_FAILURE;
    }

    bool debug_mode = false;
    bool no_stream = false;
    bool json_mode = false;
    int message_arg_index = -1;
    int subagent_mode = 0;
    char *subagent_task = NULL;
    char *subagent_context = NULL;

    bool yolo_mode = false;
    const char *cli_allow_entries[MAX_CLI_ALLOW_ENTRIES];
    int cli_allow_count = 0;
    const char *cli_allow_categories[MAX_CLI_ALLOW_CATEGORIES];
    int cli_allow_category_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "--no-stream") == 0) {
            no_stream = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else if (strcmp(argv[i], "--yolo") == 0) {
            yolo_mode = true;
        } else if (strcmp(argv[i], "--allow") == 0 && i + 1 < argc) {
            if (cli_allow_count < MAX_CLI_ALLOW_ENTRIES) {
                cli_allow_entries[cli_allow_count++] = argv[++i];
            } else {
                fprintf(stderr, "Warning: Too many --allow entries (max %d)\n",
                        MAX_CLI_ALLOW_ENTRIES);
                i++;
            }
        } else if (strncmp(argv[i], "--allow-category=", 17) == 0) {
            if (cli_allow_category_count < MAX_CLI_ALLOW_CATEGORIES) {
                cli_allow_categories[cli_allow_category_count++] = argv[i] + 17;
            } else {
                fprintf(stderr, "Warning: Too many --allow-category entries (max %d)\n",
                        MAX_CLI_ALLOW_CATEGORIES);
            }
        } else if (strcmp(argv[i], "--subagent") == 0) {
            subagent_mode = 1;
        } else if (strcmp(argv[i], "--task") == 0 && i + 1 < argc) {
            subagent_task = argv[++i];
        } else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc) {
            subagent_context = argv[++i];
        } else if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            i++; // Already processed in early parse pass
        } else if (message_arg_index == -1 && argv[i][0] != '-') {
            message_arg_index = i;
        }
    }

    if (subagent_mode) {
        if (subagent_task == NULL) {
            fprintf(stderr, "Error: --subagent requires --task argument\n");
            return EXIT_FAILURE;
        }
        debug_init(debug_mode);
        return ralph_run_as_subagent(subagent_task, subagent_context);
    }

    debug_init(debug_mode);

    if (message_arg_index != -1) {
        const char *user_message = argv[message_arg_index];

        RalphSession session;
        if (ralph_init_session(&session) != 0) {
            fprintf(stderr, "Error: Failed to initialize Ralph session\n");
            return EXIT_FAILURE;
        }

        if (ralph_load_config(&session) != 0) {
            fprintf(stderr, "Error: Failed to load Ralph configuration\n");
            ralph_cleanup_session(&session);
            return EXIT_FAILURE;
        }

        apply_gate_cli_overrides(&session, yolo_mode, cli_allow_categories,
                                 cli_allow_category_count, cli_allow_entries, cli_allow_count);

        if (no_stream) {
            session.session_data.config.enable_streaming = false;
        }

        if (json_mode) {
            session.session_data.config.json_output_mode = true;
            set_json_output_mode(true);
            json_output_init();
        }

        int result = ralph_process_message(&session, user_message);

        ralph_cleanup_session(&session);
        return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        if (!json_mode) {
            printf("\033[1mRalph\033[0m - AI Assistant\n");
            printf("Commands: quit, exit | Ctrl+D to end\n\n");
        }

        RalphSession session;
        if (ralph_init_session(&session) != 0) {
            fprintf(stderr, "Error: Failed to initialize Ralph session\n");
            return EXIT_FAILURE;
        }

        if (ralph_load_config(&session) != 0) {
            fprintf(stderr, "Error: Failed to load Ralph configuration\n");
            ralph_cleanup_session(&session);
            return EXIT_FAILURE;
        }

        apply_gate_cli_overrides(&session, yolo_mode, cli_allow_categories,
                                 cli_allow_category_count, cli_allow_entries, cli_allow_count);

        if (no_stream) {
            session.session_data.config.enable_streaming = false;
        }

        if (json_mode) {
            session.session_data.config.json_output_mode = true;
            set_json_output_mode(true);
            json_output_init();
        }

        if (!json_mode) {
            if (session.session_data.conversation.count == 0) {
                debug_printf("Generating welcome message...\n");

                const char* greeting_prompt = "This is your first interaction with this user in interactive mode. "
                                            "Please introduce yourself as Ralph, briefly explain your capabilities "
                                            "(answering questions, running shell commands, file operations, problem-solving), "
                                            "and ask what you can help with today. Keep it warm, concise, and engaging. "
                                            "Make it feel personal and conversational, not like a static template.";

                int result = ralph_process_message(&session, greeting_prompt);
                if (result != 0) {
                    printf("Hello! I'm Ralph, your AI assistant. What can I help you with today?\n");
                }
                printf("\n");
            } else {
                debug_printf("Generating recap of recent conversation (%d messages)...\n",
                           session.session_data.conversation.count);

                int result = ralph_generate_recap(&session, 5);
                if (result != 0) {
                    printf("Welcome back! Ready to continue where we left off.\n");
                }
                printf("\n");
            }
        }
        
        using_history();
        memory_commands_init();

        if (interrupt_init() != 0) {
            fprintf(stderr, "Warning: Failed to initialize interrupt handling\n");
        }

        char *input_line;

        while (1) {
            interrupt_clear();

            input_line = readline("> ");

            if (input_line == NULL) {
                if (interrupt_pending()) {
                    interrupt_clear();
                    printf("\n");
                    continue;
                }
                printf("\n");
                break;
            }

            if (strcmp(input_line, "quit") == 0 || strcmp(input_line, "exit") == 0) {
                printf("Goodbye!\n");
                free(input_line);
                break;
            }

            if (strlen(input_line) == 0) {
                free(input_line);
                continue;
            }

            add_history(input_line);

            if (input_line[0] == '/') {
                if (process_memory_command(input_line) == 0) {
                    free(input_line);
                    continue;
                }
            }

            printf("\n");
            int result = ralph_process_message(&session, input_line);
            if (result == -2) {
                printf("Operation cancelled.\n");
            } else if (result != 0) {
                fprintf(stderr, "Error: Failed to process message\n");
            }
            printf("\n");

            free(input_line);
        }

        interrupt_cleanup();
        memory_commands_cleanup();
        ralph_cleanup_session(&session);
        return EXIT_SUCCESS;
    }
}
