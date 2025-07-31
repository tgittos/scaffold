#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ralph.h"

#define MAX_INPUT_SIZE 8192

int main(int argc, char *argv[])
{
    // Check if single message mode or interactive mode
    if (argc == 2) {
        // Single message mode (original behavior)
        const char *user_message = argv[1];
        
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
    } else if (argc == 1) {
        // Interactive mode
        printf("Ralph Interactive Mode\n");
        printf("Type 'quit' or 'exit' to end the conversation.\n");
        printf("Press Enter on empty line to send multi-line messages.\n\n");
        
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
        if (session.conversation.count == 0) {
            fprintf(stderr, "Generating welcome message...\n");
            
            // Send a system message to get an AI-generated greeting
            const char* greeting_prompt = "This is your first interaction with this user in interactive mode. "
                                        "Please introduce yourself as Ralph, briefly explain your capabilities "
                                        "(answering questions, running shell commands, file operations, problem-solving), "
                                        "and ask what you can help with today. Keep it warm, concise, and engaging. "
                                        "Make it feel personal and conversational, not like a static template.";
            
            int result = ralph_process_message(&session, greeting_prompt);
            if (result != 0) {
                // Fallback to basic message if AI greeting fails
                printf("Hello! I'm Ralph, your AI assistant. What can I help you with today?\n");
            }
            printf("\n");
        }
        
        char input_buffer[MAX_INPUT_SIZE];
        
        while (1) {
            printf("> ");
            fflush(stdout);
            
            // Read input line
            if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
                printf("\n");
                break;
            }
            
            // Remove trailing newline
            size_t len = strlen(input_buffer);
            if (len > 0 && input_buffer[len-1] == '\n') {
                input_buffer[len-1] = '\0';
            }
            
            // Check for exit commands
            if (strcmp(input_buffer, "quit") == 0 || strcmp(input_buffer, "exit") == 0) {
                printf("Goodbye!\n");
                break;
            }
            
            // Skip empty messages
            if (strlen(input_buffer) == 0) {
                continue;
            }
            
            // Process the user message
            printf("\n");
            int result = ralph_process_message(&session, input_buffer);
            if (result != 0) {
                fprintf(stderr, "Error: Failed to process message\n");
            }
            printf("\n");
        }
        
        // Cleanup
        ralph_cleanup_session(&session);
        return EXIT_SUCCESS;
    } else {
        // Invalid arguments
        fprintf(stderr, "Usage: %s [\"<message>\"]\n", argv[0]);
        fprintf(stderr, "  No arguments: Interactive mode\n");
        fprintf(stderr, "  With message: Single message mode\n");
        fprintf(stderr, "Example: %s \"Hello, how are you?\"\n", argv[0]);
        return EXIT_FAILURE;
    }
}