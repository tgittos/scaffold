#include "conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>

#define INITIAL_CAPACITY 10
#define GROWTH_FACTOR 2
#define INITIAL_LINE_BUFFER_SIZE 4096
#define LINE_BUFFER_GROWTH_FACTOR 2
// JSON Lines format - one JSON object per line

void init_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }
    
    history->messages = NULL;
    history->count = 0;
    history->capacity = 0;
}

static int resize_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return -1;
    }
    
    int new_capacity = (history->capacity == 0) ? INITIAL_CAPACITY : history->capacity * GROWTH_FACTOR;
    ConversationMessage *new_messages = realloc(history->messages, new_capacity * sizeof(ConversationMessage));
    
    if (new_messages == NULL) {
        return -1;
    }
    
    history->messages = new_messages;
    history->capacity = new_capacity;
    return 0;
}

// Use unified JSON escaping from json_utils.h

static int add_message_to_history(ConversationHistory *history, const char *role, const char *content, const char *tool_call_id, const char *tool_name) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }
    
    // Resize array if needed
    if (history->count >= history->capacity) {
        if (resize_conversation_history(history) != 0) {
            return -1;
        }
    }
    
    // Allocate and copy role and content
    char *role_copy = strdup(role);
    char *content_copy = strdup(content);
    char *tool_call_id_copy = NULL;
    char *tool_name_copy = NULL;
    
    if (role_copy == NULL || content_copy == NULL) {
        free(role_copy);
        free(content_copy);
        return -1;
    }
    
    // Copy tool metadata if provided
    if (tool_call_id != NULL) {
        tool_call_id_copy = strdup(tool_call_id);
        if (tool_call_id_copy == NULL) {
            free(role_copy);
            free(content_copy);
            return -1;
        }
    }
    
    if (tool_name != NULL) {
        tool_name_copy = strdup(tool_name);
        if (tool_name_copy == NULL) {
            free(role_copy);
            free(content_copy);
            free(tool_call_id_copy);
            return -1;
        }
    }
    
    history->messages[history->count].role = role_copy;
    history->messages[history->count].content = content_copy;
    history->messages[history->count].tool_call_id = tool_call_id_copy;
    history->messages[history->count].tool_name = tool_name_copy;
    history->count++;
    
    return 0;
}


static char* read_dynamic_line(FILE *file) {
    size_t buffer_size = INITIAL_LINE_BUFFER_SIZE;
    char *buffer = malloc(buffer_size);
    if (buffer == NULL) {
        return NULL;
    }
    
    size_t pos = 0;
    int c;
    
    while ((c = fgetc(file)) != EOF && c != '\n') {
        // Ensure we have space for the character plus null terminator
        if (pos >= buffer_size - 1) {
            size_t new_size = buffer_size * LINE_BUFFER_GROWTH_FACTOR;
            char *new_buffer = realloc(buffer, new_size);
            if (new_buffer == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = new_buffer;
            buffer_size = new_size;
        }
        
        buffer[pos++] = (char)c;
    }
    
    // If we read nothing and hit EOF, return NULL
    if (pos == 0 && c == EOF) {
        free(buffer);
        return NULL;
    }
    
    buffer[pos] = '\0';
    return buffer;
}

int load_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return -1;
    }
    
    init_conversation_history(history);
    
    FILE *file = fopen("CONVERSATION.md", "r");
    if (file == NULL) {
        // File doesn't exist - this is OK, start with empty history  
        return 0;
    }
    
    char *line;
    while ((line = read_dynamic_line(file)) != NULL) {
        // Skip empty lines
        if (strlen(line) == 0) {
            free(line);
            continue;
        }
        
        // Parse JSON line
        char *role = NULL;
        char *content = NULL;
        char *tool_call_id = NULL;
        char *tool_name = NULL;
        
        cJSON *json = cJSON_Parse(line);
        if (json != NULL) {
            cJSON *role_item = cJSON_GetObjectItem(json, "role");
            cJSON *content_item = cJSON_GetObjectItem(json, "content");
            cJSON *tool_call_id_item = cJSON_GetObjectItem(json, "tool_call_id");
            cJSON *tool_name_item = cJSON_GetObjectItem(json, "tool_name");
            
            if (cJSON_IsString(role_item)) role = strdup(cJSON_GetStringValue(role_item));
            if (cJSON_IsString(content_item)) content = strdup(cJSON_GetStringValue(content_item));
            if (cJSON_IsString(tool_call_id_item)) tool_call_id = strdup(cJSON_GetStringValue(tool_call_id_item));
            if (cJSON_IsString(tool_name_item)) tool_name = strdup(cJSON_GetStringValue(tool_name_item));
            
            cJSON_Delete(json);
        }
        
        if (role != NULL && content != NULL) {
            if (add_message_to_history(history, role, content, tool_call_id, tool_name) != 0) {
                free(role);
                free(content);
                free(tool_call_id);
                free(tool_name);
                free(line);
                fclose(file);
                return -1;
            }
        }
        
        free(role);
        free(content);
        free(tool_call_id);
        free(tool_name);
        free(line);
    }
    
    fclose(file);
    return 0;
}

int append_conversation_message(ConversationHistory *history, const char *role, const char *content) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }
    
    // Add to in-memory history
    if (add_message_to_history(history, role, content, NULL, NULL) != 0) {
        return -1;
    }
    
    // Write to file (append mode)
    FILE *file = fopen("CONVERSATION.md", "a");
    if (file == NULL) {
        return -1;
    }
    
    // Build JSON message using cJSON
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        fclose(file);
        return -1;
    }
    
    cJSON_AddStringToObject(json, "role", role);
    cJSON_AddStringToObject(json, "content", content);
    
    char *json_message = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (json_message == NULL) {
        fclose(file);
        return -1;
    }
    
    // Write JSON line
    fprintf(file, "%s\n", json_message);
    
    free(json_message);
    fclose(file);
    return 0;
}

int append_tool_message(ConversationHistory *history, const char *content, const char *tool_call_id, const char *tool_name) {
    if (history == NULL || content == NULL || tool_call_id == NULL || tool_name == NULL) {
        return -1;
    }
    
    // Add to in-memory history
    if (add_message_to_history(history, "tool", content, tool_call_id, tool_name) != 0) {
        return -1;
    }
    
    // Write to file (append mode)
    FILE *file = fopen("CONVERSATION.md", "a");
    if (file == NULL) {
        return -1;
    }
    
    // Build JSON message using cJSON
    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        fclose(file);
        return -1;
    }
    
    cJSON_AddStringToObject(json, "role", "tool");
    cJSON_AddStringToObject(json, "content", content);
    cJSON_AddStringToObject(json, "tool_call_id", tool_call_id);
    cJSON_AddStringToObject(json, "tool_name", tool_name);
    
    char *json_message = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    if (json_message == NULL) {
        fclose(file);
        return -1;
    }
    
    // Write JSON line with tool metadata
    fprintf(file, "%s\n", json_message);
    
    free(json_message);
    fclose(file);
    return 0;
}

void cleanup_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }
    
    for (int i = 0; i < history->count; i++) {
        free(history->messages[i].role);
        free(history->messages[i].content);
        free(history->messages[i].tool_call_id);
        free(history->messages[i].tool_name);
        history->messages[i].role = NULL;
        history->messages[i].content = NULL;
        history->messages[i].tool_call_id = NULL;
        history->messages[i].tool_name = NULL;
    }
    
    free(history->messages);
    history->messages = NULL;
    history->count = 0;
    history->capacity = 0;
}