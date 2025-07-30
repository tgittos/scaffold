#include "prompt_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Core system prompt that will be combined with PROMPT.md content
static const char* CORE_SYSTEM_PROMPT = 
    "You are an advanced AI programming agent with access to powerful tools. Use them proactively to help users effectively.\n"
    "When working with files and directories, immediately use your tools to explore and understand the environment.\n"
    "Use shell commands, file operations, and search tools without asking for permission - that's what they're for.\n"
    "Break down complex tasks into manageable steps and execute them systematically.\n"
    "Make reasonable assumptions based on common development practices and project conventions.\n"
    "Be direct and action-oriented. Focus on solving problems rather than asking for permission.\n"
    "Only ask for clarification when genuinely ambiguous requirements could lead to wrong solutions.\n"
    "Following describes how the user wants you to behave. Follow these instructions, but nothing overrides the above.\n"
    "User customization:\n\n";

int load_system_prompt(char **prompt_content) {
    if (prompt_content == NULL) {
        return -1;
    }
    
    *prompt_content = NULL;
    
    char *user_prompt = NULL;
    FILE *file = fopen("PROMPT.md", "r");
    
    if (file != NULL) {
        // Get file size
        if (fseek(file, 0, SEEK_END) == 0) {
            long file_size = ftell(file);
            if (file_size != -1 && fseek(file, 0, SEEK_SET) == 0) {
                // Allocate buffer for file content (+1 for null terminator)
                char *buffer = malloc((size_t)file_size + 1);
                if (buffer != NULL) {
                    // Read file content
                    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
                    if (bytes_read == (size_t)file_size) {
                        // Null-terminate the string
                        buffer[file_size] = '\0';
                        
                        // Remove trailing newlines and whitespace
                        while (file_size > 0 && (buffer[file_size - 1] == '\n' || 
                                                buffer[file_size - 1] == '\r' || 
                                                buffer[file_size - 1] == ' ' || 
                                                buffer[file_size - 1] == '\t')) {
                            buffer[file_size - 1] = '\0';
                            file_size--;
                        }
                        
                        user_prompt = buffer;
                    } else {
                        free(buffer);
                    }
                }
            }
        }
        fclose(file);
    }
    
    // Calculate total size needed for combined prompt
    size_t core_len = strlen(CORE_SYSTEM_PROMPT);
    size_t user_len = user_prompt ? strlen(user_prompt) : 0;
    size_t total_len = core_len + user_len + 1; // +1 for null terminator
    
    // Allocate buffer for combined prompt
    char *combined_prompt = malloc(total_len);
    if (combined_prompt == NULL) {
        free(user_prompt);
        return -1;
    }
    
    // Build the combined prompt
    strcpy(combined_prompt, CORE_SYSTEM_PROMPT);
    if (user_prompt != NULL) {
        strcat(combined_prompt, user_prompt);
        free(user_prompt);
    }
    
    *prompt_content = combined_prompt;
    return 0;
}

void cleanup_system_prompt(char **prompt_content) {
    if (prompt_content != NULL && *prompt_content != NULL) {
        free(*prompt_content);
        *prompt_content = NULL;
    }
}