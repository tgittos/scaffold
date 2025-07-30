#include "prompt_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int load_system_prompt(char **prompt_content) {
    if (prompt_content == NULL) {
        return -1;
    }
    
    *prompt_content = NULL;
    
    FILE *file = fopen("PROMPT.md", "r");
    if (file == NULL) {
        return -1;  // File not found or cannot be opened
    }
    
    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    
    long file_size = ftell(file);
    if (file_size == -1) {
        fclose(file);
        return -1;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    
    // Allocate buffer for file content (+1 for null terminator)
    char *buffer = malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return -1;
    }
    
    // Read file content
    size_t bytes_read = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return -1;
    }
    
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
    
    *prompt_content = buffer;
    return 0;
}

void cleanup_system_prompt(char **prompt_content) {
    if (prompt_content != NULL && *prompt_content != NULL) {
        free(*prompt_content);
        *prompt_content = NULL;
    }
}