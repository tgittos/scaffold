#include "conversation_tracker.h"
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

// Escape JSON string content
static char* escape_json_string(const char *content) {
    if (content == NULL) {
        return strdup("");
    }
    
    size_t len = strlen(content);
    // Worst case: every character needs escaping, plus null terminator
    char *escaped = malloc(len * 2 + 1);
    if (escaped == NULL) {
        return NULL;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (content[i]) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            case '\b':
                escaped[j++] = '\\';
                escaped[j++] = 'b';
                break;
            case '\f':
                escaped[j++] = '\\';
                escaped[j++] = 'f';
                break;
            default:
                if ((unsigned char)content[i] < 32) {
                    // Escape other control characters
                    sprintf(&escaped[j], "\\u%04x", (unsigned char)content[i]);
                    j += 6;
                } else {
                    escaped[j++] = content[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}

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

// Simple JSON parser for conversation messages
static char* extract_json_field(const char *json, const char *field_name) {
    char search_pattern[256] = {0};
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\":", field_name);
    
    char *start = strstr(json, search_pattern);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_pattern);
    
    // Skip optional whitespace
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    
    // Expect opening quote
    if (*start != '"') {
        return NULL;
    }
    start++;
    char *end = start;
    
    // Find the end of the string value, handling escaped quotes
    while (*end != '\0') {
        if (*end == '"' && (end == start || *(end - 1) != '\\')) {
            break;
        }
        end++;
    }
    
    if (*end != '"') {
        return NULL;
    }
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }
    
    strncpy(result, start, len);
    result[len] = '\0';
    
    // Unescape JSON string
    char *unescaped = malloc(len + 1);
    if (unescaped == NULL) {
        free(result);
        return NULL;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (result[i] == '\\' && i + 1 < len) {
            switch (result[i + 1]) {
                case '"': unescaped[j++] = '"'; i++; break;
                case '\\': unescaped[j++] = '\\'; i++; break;
                case 'n': unescaped[j++] = '\n'; i++; break;
                case 'r': unescaped[j++] = '\r'; i++; break;
                case 't': unescaped[j++] = '\t'; i++; break;
                case 'b': unescaped[j++] = '\b'; i++; break;
                case 'f': unescaped[j++] = '\f'; i++; break;
                default: unescaped[j++] = result[i]; break;
            }
        } else {
            unescaped[j++] = result[i];
        }
    }
    unescaped[j] = '\0';
    
    free(result);
    return unescaped;
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
    
    char *escaped_content = escape_json_string(content);
    if (escaped_content == NULL) {
        fclose(file);
        return -1;
    }
    
    // Write JSON line
    fprintf(file, "{\"role\":\"%s\",\"content\":\"%s\"}\n", role, escaped_content);
    
    free(escaped_content);
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
    
    char *escaped_content = escape_json_string(content);
    if (escaped_content == NULL) {
        fclose(file);
        return -1;
    }
    
    // Write JSON line with tool metadata
    fprintf(file, "{\"role\":\"tool\",\"content\":\"%s\",\"tool_call_id\":\"%s\",\"tool_name\":\"%s\"}\n", 
            escaped_content, tool_call_id, tool_name);
    
    free(escaped_content);
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