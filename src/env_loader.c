#include "env_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char* trim_whitespace(char *str)
{
    char *end;
    
    // Trim leading space
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str; // All spaces?
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    
    // Write new null terminator
    end[1] = '\0';
    
    return str;
}

static int parse_env_line(char *line)
{
    char *equals_pos = strchr(line, '=');
    if (equals_pos == NULL) {
        return 0; // Skip lines without '='
    }
    
    // Split into key and value
    *equals_pos = '\0';
    char *key = trim_whitespace(line);
    char *value = trim_whitespace(equals_pos + 1);
    
    // Skip empty keys
    if (strlen(key) == 0) {
        return 0;
    }
    
    // Set environment variable
    if (setenv(key, value, 1) != 0) {
        fprintf(stderr, "Error: Failed to set environment variable %s\n", key);
        return -1;
    }
    
    return 0;
}

int load_env_file(const char *filepath)
{
    FILE *file = NULL;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int result = 0;
    
    if (filepath == NULL) {
        fprintf(stderr, "Error: Invalid filepath parameter\n");
        return -1;
    }
    
    file = fopen(filepath, "r");
    if (file == NULL) {
        // Not finding .env file is not necessarily an error - it's optional
        return 0;
    }
    
    while ((read = getline(&line, &len, file)) != -1) {
        // Remove newline character
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }
        
        // Skip empty lines and comments (lines starting with #)
        char *trimmed = trim_whitespace(line);
        if (strlen(trimmed) == 0 || trimmed[0] == '#') {
            continue;
        }
        
        if (parse_env_line(trimmed) != 0) {
            result = -1;
            break;
        }
    }
    
    free(line);
    fclose(file);
    return result;
}