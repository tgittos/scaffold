#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "ralph.h"
#include "debug_output.h"
#include "../cli/memory_commands.h"

#define MAX_INPUT_SIZE 8192

int main(int argc, char *argv[])
{
    // Parse command line arguments for debug flag
    bool debug_mode = false;
    int message_arg_index = -1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (message_arg_index == -1 && argv[i][0] != '-') {
            message_arg_index = i;
        }
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
        
        // Process the user message
        int result = ralph_process_message(&session, user_message);
        
        // Cleanup and return result
        ralph_cleanup_session(&session);
        return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    } else if (argc == 1 || (argc == 2 && debug_mode)) {
        // Interactive mode
        printf("\033[1mRalph\033[0m - AI Assistant\n");
        printf("Commands: quit, exit | Ctrl+D to end\n\n");
        
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
        
        // Check if this is a first-time user (no conversation history)
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
    } else {
        // Invalid arguments
        fprintf(stderr, "\033[1mUsage:\033[0m %s [options] [message]\n\n", argv[0]);
        fprintf(stderr, "\033[1mModes:\033[0m\n");
        fprintf(stderr, "  %s                    Interactive chat mode\n", argv[0]);
        fprintf(stderr, "  %s \"question\"         Single question mode\n\n", argv[0]);
        fprintf(stderr, "\033[1mOptions:\033[0m\n");
        fprintf(stderr, "  --debug               Show API debug output\n\n");
        fprintf(stderr, "\033[1mSetup:\033[0m\n");
        fprintf(stderr, "  1. Export OPENAI_API_KEY=<your-key> or\n");
        fprintf(stderr, "  2. Edit ./ralph.config.json after first run\n");
        fprintf(stderr, "Example: %s \"Hello, how are you?\"\n", argv[0]);
        fprintf(stderr, "Example: %s --debug \"Hello, how are you?\"\n", argv[0]);
        return EXIT_FAILURE;
    }
}