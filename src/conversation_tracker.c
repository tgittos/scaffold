#include "conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 10
#define GROWTH_FACTOR 2
#define FIELD_SEPARATOR "\x1F"  // ASCII Unit Separator (rarely used in text)

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

// Escape field separators and newlines in content for serialization
static char* escape_content_for_serialization(const char *content) {
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
        if (content[i] == '\x1F') {
            // Escape field separator
            escaped[j++] = '\\';
            escaped[j++] = 'F';
        } else if (content[i] == '\n') {
            // Escape newlines
            escaped[j++] = '\\';
            escaped[j++] = 'n';
        } else if (content[i] == '\\') {
            // Escape backslashes
            escaped[j++] = '\\';
            escaped[j++] = '\\';
        } else if (content[i] == '\r') {
            // Skip carriage returns
            continue;
        } else {
            escaped[j++] = content[i];
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}

// Unescape content from serialization format
static char* unescape_content_from_serialization(const char *content) {
    if (content == NULL) {
        return strdup("");
    }
    
    size_t len = strlen(content);
    char *unescaped = malloc(len + 1);
    if (unescaped == NULL) {
        return NULL;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (content[i] == '\\' && i + 1 < len) {
            if (content[i + 1] == 'F') {
                unescaped[j++] = '\x1F';
                i++; // Skip the 'F'
            } else if (content[i + 1] == 'n') {
                unescaped[j++] = '\n';
                i++; // Skip the 'n'
            } else if (content[i + 1] == '\\') {
                unescaped[j++] = '\\';
                i++; // Skip the second backslash
            } else {
                unescaped[j++] = content[i];
            }
        } else {
            unescaped[j++] = content[i];
        }
    }
    unescaped[j] = '\0';
    
    return unescaped;
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
    
    char line[8192];
    memset(line, 0, sizeof(line)); // Initialize buffer
    while (fgets(line, sizeof(line), file)) {
        // Ensure the line is null-terminated
        line[sizeof(line) - 1] = '\0';
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        // Skip empty lines
        if (strlen(line) == 0) {
            continue;
        }
        
        // Parse serialized format: role<SEP>content<SEP>tool_call_id<SEP>tool_name
        // Manual parsing to handle empty fields correctly - make a copy to avoid corruption
        char *line_copy = strdup(line);
        if (line_copy == NULL) {
            fclose(file);
            return -1;
        }
        
        char *role = NULL;
        char *content = NULL;
        char *tool_call_id = NULL;
        char *tool_name = NULL;
        
        char *pos = line_copy;
        char *start = pos;
        int field_num = 0;
        
        while (*pos != '\0') {
            if (*pos == '\x1F') {
                *pos = '\0'; // Null-terminate current field
                
                switch (field_num) {
                    case 0:
                        role = start;
                        break;
                    case 1:
                        content = start;
                        break;  
                    case 2:
                        if (strlen(start) > 0) tool_call_id = start;
                        break;
                    case 3:
                        if (strlen(start) > 0) tool_name = start;
                        break;
                }
                
                field_num++;
                start = pos + 1;
            }
            pos++;
        }
        
        // Handle the last field (no trailing separator)
        if (field_num <= 3) {
            switch (field_num) {
                case 0:
                    role = start;
                    break;
                case 1:
                    content = start;
                    break;
                case 2:
                    if (strlen(start) > 0) tool_call_id = start;
                    break;
                case 3:
                    if (strlen(start) > 0) tool_name = start;
                    break;
            }
        }
        
        if (role != NULL && content != NULL) {
            char *unescaped_content = unescape_content_from_serialization(content);
            if (unescaped_content != NULL) {
                if (add_message_to_history(history, role, unescaped_content, tool_call_id, tool_name) != 0) {
                    free(unescaped_content);
                    free(line_copy);
                    fclose(file);
                    return -1;
                }
                free(unescaped_content);
            }
        }
        
        free(line_copy);
        memset(line, 0, sizeof(line)); // Clear buffer for next iteration
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
    
    char *escaped_content = escape_content_for_serialization(content);
    if (escaped_content == NULL) {
        fclose(file);
        return -1;
    }
    
    // Format: role<SEP>content<SEP><SEP>\n (empty tool fields)
    fprintf(file, "%s%s%s%s%s\n", role, FIELD_SEPARATOR, escaped_content, FIELD_SEPARATOR, FIELD_SEPARATOR);
    
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
    
    char *escaped_content = escape_content_for_serialization(content);
    if (escaped_content == NULL) {
        fclose(file);
        return -1;
    }
    
    // Format: tool<SEP>content<SEP>tool_call_id<SEP>tool_name\n
    fprintf(file, "tool%s%s%s%s%s%s\n", FIELD_SEPARATOR, escaped_content, FIELD_SEPARATOR, tool_call_id, FIELD_SEPARATOR, tool_name);
    
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