#include "conversation_tracker.h"
#include "json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 10
#define GROWTH_FACTOR 2
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

// Wrapper around unified JSON parser for backward compatibility
static char* extract_json_field(const char *json, const char *field_name) {
    JsonParser parser = {0};
    if (json_parser_init(&parser, json) != 0) {
        return NULL;
    }
    
    return json_parser_extract_string(&parser, field_name);
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
    
    while (1) {
        char line[16384] = {0};
        if (!fgets(line, sizeof(line), file)) {
            break;
        }
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        // Skip empty lines
        if (strlen(line) == 0) {
            continue;
        }
        
        // Parse JSON line
        char *role = extract_json_field(line, "role");
        char *content = extract_json_field(line, "content");
        char *tool_call_id = extract_json_field(line, "tool_call_id");
        char *tool_name = extract_json_field(line, "tool_name");
        
        if (role != NULL && content != NULL) {
            if (add_message_to_history(history, role, content, tool_call_id, tool_name) != 0) {
                free(role);
                free(content);
                free(tool_call_id);
                free(tool_name);
                fclose(file);
                return -1;
            }
        }
        
        free(role);
        free(content);
        free(tool_call_id);
        free(tool_name);
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
    
    // Build JSON message using unified builder
    char *json_message = json_build_message(role, content);
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
    
    // Build JSON message using unified builder
    JsonBuilder builder = {0};
    if (json_builder_init(&builder) != 0) {
        fclose(file);
        return -1;
    }
    
    json_builder_start_object(&builder);
    json_builder_add_string(&builder, "role", "tool");
    json_builder_add_separator(&builder);
    json_builder_add_string(&builder, "content", content);
    json_builder_add_separator(&builder);
    json_builder_add_string(&builder, "tool_call_id", tool_call_id);
    json_builder_add_separator(&builder);
    json_builder_add_string(&builder, "tool_name", tool_name);
    json_builder_end_object(&builder);
    
    char *json_message = json_builder_finalize(&builder);
    json_builder_cleanup(&builder);
    
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