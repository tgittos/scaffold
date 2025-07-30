#include <stdio.h>
#include <stdlib.h>
#include "ralph.h"

int main(int argc, char *argv[])
{
    // Validate command line arguments
    if (argc != 2) {
        fprintf(stderr, "Usage: %s \"<message>\"\n", argv[0]);
        fprintf(stderr, "Example: %s \"Hello, how are you?\"\n", argv[0]);
        return EXIT_FAILURE;
    }
    
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
}