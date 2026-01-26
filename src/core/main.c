#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ralph.h"
#include "debug_output.h"
#include "output_formatter.h"
#include "json_output.h"
#include "../cli/memory_commands.h"
#include "../tools/subagent_tool.h"
#include "../utils/ralph_home.h"

#define MAX_INPUT_SIZE 8192
#define RALPH_VERSION "0.1.0"

static void print_version(void) {
    printf("ralph %s\n", RALPH_VERSION);
}

static void print_help(const char *program_name) {
    printf("ralph %s - AI Assistant\n\n", RALPH_VERSION);
    printf("Usage: %s [OPTIONS] [MESSAGE]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help        Show this help message and exit\n");
    printf("  -v, --version     Show version information and exit\n");
    printf("  --debug           Enable debug output (shows HTTP requests and data exchange)\n");
    printf("  --no-stream       Disable response streaming\n");
    printf("  --json            Enable JSON output mode\n");
    printf("  --home <path>     Override Ralph home directory (default: ~/.local/ralph)\n");
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

int main(int argc, char *argv[])
{
    // Handle --version and --help first, before any initialization
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

    // Parse --home flag FIRST, before any initialization
    // This must be done early because other modules depend on ralph_home
    char *home_override = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            home_override = argv[++i];
            break;
        }
    }

    // Initialize ralph home directory before anything else
    if (ralph_home_init(home_override) != 0) {
        fprintf(stderr, "Error: Failed to initialize ralph home directory\n");
        return EXIT_FAILURE;
    }

    // Parse remaining command line arguments
    bool debug_mode = false;
    bool no_stream = false;
    bool json_mode = false;
    int message_arg_index = -1;
    int subagent_mode = 0;
    char *subagent_task = NULL;
    char *subagent_context = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (strcmp(argv[i], "--no-stream") == 0) {
            no_stream = true;
        } else if (strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else if (strcmp(argv[i], "--subagent") == 0) {
            subagent_mode = 1;
        } else if (strcmp(argv[i], "--task") == 0 && i + 1 < argc) {
            subagent_task = argv[++i];
        } else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc) {
            subagent_context = argv[++i];
        } else if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) {
            // Already processed, skip the value
            i++;
        } else if (message_arg_index == -1 && argv[i][0] != '-') {
            message_arg_index = i;
        }
    }

    // Handle subagent mode
    if (subagent_mode) {
        if (subagent_task == NULL) {
            fprintf(stderr, "Error: --subagent requires --task argument\n");
            return EXIT_FAILURE;
        }
        // Initialize debug system for subagent mode
        debug_init(debug_mode);
        return ralph_run_as_subagent(subagent_task, subagent_context);
    }

    // Initialize debug system
    debug_init(debug_mode);
    
    // Check if single message mode or interactive mode
    if (message_arg_index != -1) {
        // Single message mode (original behavior)
        const char *user_message = argv[message_arg_index];
        
        // Initialize Ralph session
        RalphSession session;
        if (ralph_init_session(&session) != 0) {
            fprintf(stderr, "Error: Failed to initialize Ralph session\n");
            return EXIT_FAILURE;
        }
        
        // Load configuration
        if (ralph_load_config(&session) != 0) {
            fprintf(stderr, "Error: Failed to load Ralph configuration\n");
            ralph_cleanup_session(&session);
            return EXIT_FAILURE;
        }

        // Override streaming if --no-stream flag was passed
        if (no_stream) {
            session.session_data.config.enable_streaming = false;
        }

        // Enable JSON output mode if --json flag was passed
        if (json_mode) {
            session.session_data.config.json_output_mode = true;
            set_json_output_mode(true);
            json_output_init();
        }

        // Process the user message
        int result = ralph_process_message(&session, user_message);
        
        // Cleanup and return result
        ralph_cleanup_session(&session);
        return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else {
        // Interactive mode (no message argument provided)
        if (!json_mode) {
            printf("\033[1mRalph\033[0m - AI Assistant\n");
            printf("Commands: quit, exit | Ctrl+D to end\n\n");
        }

        // Initialize Ralph session
        RalphSession session;
        if (ralph_init_session(&session) != 0) {
            fprintf(stderr, "Error: Failed to initialize Ralph session\n");
            return EXIT_FAILURE;
        }

        // Load configuration
        if (ralph_load_config(&session) != 0) {
            fprintf(stderr, "Error: Failed to load Ralph configuration\n");
            ralph_cleanup_session(&session);
            return EXIT_FAILURE;
        }

        // Override streaming if --no-stream flag was passed
        if (no_stream) {
            session.session_data.config.enable_streaming = false;
        }

        // Enable JSON output mode if --json flag was passed
        if (json_mode) {
            session.session_data.config.json_output_mode = true;
            set_json_output_mode(true);
            json_output_init();
        }

        // Check if this is a first-time user (no conversation history)
        // Skip welcome/recap message in JSON mode
        if (!json_mode) {
            if (session.session_data.conversation.count == 0) {
                debug_printf("Generating welcome message...\n");

                // Send a system message to get an AI-generated greeting
                const char* greeting_prompt = "This is your first interaction with this user in interactive mode. "
                                            "Please introduce yourself as Ralph, briefly explain your capabilities "
                                            "(answering questions, running shell commands, file operations, problem-solving), "
                                            "and ask what you can help with today. Keep it warm, concise, and engaging. "
                                            "Make it feel personal and conversational, not like a static template.";

                // Send initial greeting (indicator will be cleared immediately)
                int result = ralph_process_message(&session, greeting_prompt);
                if (result != 0) {
                    // Fallback to basic message if AI greeting fails
                    printf("Hello! I'm Ralph, your AI assistant. What can I help you with today?\n");
                }
                printf("\n");
            } else {
                // Existing conversation - generate a brief recap
                debug_printf("Generating recap of recent conversation (%d messages)...\n",
                           session.session_data.conversation.count);

                int result = ralph_generate_recap(&session, 5);
                if (result != 0) {
                    // Fallback to simple message if recap fails
                    printf("Welcome back! Ready to continue where we left off.\n");
                }
                printf("\n");
            }
        }
        
        // Initialize readline
        using_history();
        
        // Initialize memory commands
        memory_commands_init();
        
        char *input_line;
        
        while (1) {
            // Read input line with readline
            input_line = readline("> ");
            
            // Check for EOF (Ctrl+D)
            if (input_line == NULL) {
                printf("\n");
                break;
            }
            
            // Check for exit commands
            if (strcmp(input_line, "quit") == 0 || strcmp(input_line, "exit") == 0) {
                printf("Goodbye!\n");
                free(input_line);
                break;
            }
            
            // Skip empty messages
            if (strlen(input_line) == 0) {
                free(input_line);
                continue;
            }
            
            // Add non-empty lines to history
            add_history(input_line);
            
            // Check for slash commands
            if (input_line[0] == '/') {
                // Process memory commands
                if (process_memory_command(input_line) == 0) {
                    // Command was handled
                    free(input_line);
                    continue;
                }
                // If not a recognized slash command, let it fall through to AI
            }
            
            // Process the user message
            printf("\n");
            int result = ralph_process_message(&session, input_line);
            if (result != 0) {
                fprintf(stderr, "Error: Failed to process message\n");
            }
            printf("\n");
            
            // Free the input line
            free(input_line);
        }
        
        // Cleanup
        memory_commands_cleanup();
        ralph_cleanup_session(&session);
        return EXIT_SUCCESS;
    }
}
