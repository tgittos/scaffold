#include "conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 10
#define GROWTH_FACTOR 2

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

static char* trim_whitespace(char *str) {
    if (str == NULL) {
        return NULL;
    }
    
    // Trim leading whitespace
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    if (*str == '\0') {
        return str;
    }
    
    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }
    
    return str;
}

static char* escape_markdown_content(const char *content) {
    if (content == NULL) {
        return NULL;
    }
    
    size_t len = strlen(content);
    // Worst case: every character needs escaping, plus null terminator
    char *escaped = malloc(len * 2 + 1);
    if (escaped == NULL) {
        return NULL;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        // Escape newlines as literal \n for markdown storage
        if (content[i] == '\n') {
            escaped[j++] = '\\';
            escaped[j++] = 'n';
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

static char* unescape_markdown_content(const char *content) {
    if (content == NULL) {
        return NULL;
    }
    
    size_t len = strlen(content);
    char *unescaped = malloc(len + 1);
    if (unescaped == NULL) {
        return NULL;
    }
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (content[i] == '\\' && i + 1 < len && content[i + 1] == 'n') {
            unescaped[j++] = '\n';
            i++; // Skip the 'n'
        } else {
            unescaped[j++] = content[i];
        }
    }
    unescaped[j] = '\0';
    
    return unescaped;
}

static int add_message_to_history(ConversationHistory *history, const char *role, const char *content) {
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
    
    if (role_copy == NULL || content_copy == NULL) {
        free(role_copy);
        free(content_copy);
        return -1;
    }
    
    history->messages[history->count].role = role_copy;
    history->messages[history->count].content = content_copy;
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
    
    char *current_role = NULL;
    char *current_content = NULL;
    
    char line[4096];
    memset(line, 0, sizeof(line));
    
    while (fgets(line, sizeof(line), file)) {
        // Ensure line is properly null-terminated
        line[sizeof(line) - 1] = '\0';
        char *trimmed = trim_whitespace(line);
        
        // Skip empty lines
        if (strlen(trimmed) == 0) {
            continue;
        }
        
        // Check for role headers
        if (strncmp(trimmed, "## User:", 8) == 0) {
            // Save previous message if exists
            if (current_role != NULL && current_content != NULL) {
                char *unescaped_content = unescape_markdown_content(current_content);
                if (unescaped_content != NULL) {
                    if (add_message_to_history(history, current_role, unescaped_content) != 0) {
                        free(unescaped_content);
                        free(current_role);
                        free(current_content);
                        fclose(file);
                        return -1;
                    }
                    free(unescaped_content);
                }
                free(current_role);
                free(current_content);
            }
            
            current_role = strdup("user");
            current_content = strdup("");
        } else if (strncmp(trimmed, "## Assistant:", 13) == 0) {
            // Save previous message if exists
            if (current_role != NULL && current_content != NULL) {
                char *unescaped_content = unescape_markdown_content(current_content);
                if (unescaped_content != NULL) {
                    if (add_message_to_history(history, current_role, unescaped_content) != 0) {
                        free(unescaped_content);
                        free(current_role);
                        free(current_content);
                        fclose(file);
                        return -1;
                    }
                    free(unescaped_content);
                }
                free(current_role);
                free(current_content);
            }
            
            current_role = strdup("assistant");
            current_content = strdup("");
        } else if (current_role != NULL) {
            // Append content line
            size_t old_len = strlen(current_content);
            size_t new_len = old_len + strlen(trimmed) + 1; // +1 for newline
            char *new_content = realloc(current_content, new_len + 1); // +1 for null terminator
            
            if (new_content == NULL) {
                free(current_role);
                free(current_content);
                fclose(file);
                return -1;
            }
            
            current_content = new_content;
            if (old_len > 0) {
                strcat(current_content, "\n");
            }
            strcat(current_content, trimmed);
        }
        
        // Clear buffer for next iteration
        memset(line, 0, sizeof(line));
    }
    
    // Save the last message if exists
    if (current_role != NULL && current_content != NULL) {
        char *unescaped_content = unescape_markdown_content(current_content);
        if (unescaped_content != NULL) {
            if (add_message_to_history(history, current_role, unescaped_content) != 0) {
                free(unescaped_content);
                free(current_role);
                free(current_content);
                fclose(file);
                return -1;
            }
            free(unescaped_content);
        }
        free(current_role);
        free(current_content);
    }
    
    fclose(file);
    return 0;
}

int append_conversation_message(ConversationHistory *history, const char *role, const char *content) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }
    
    // Add to in-memory history
    if (add_message_to_history(history, role, content) != 0) {
        return -1;
    }
    
    // Write to file (append mode)
    FILE *file = fopen("CONVERSATION.md", "a");
    if (file == NULL) {
        return -1;
    }
    
    char *escaped_content = escape_markdown_content(content);
    if (escaped_content == NULL) {
        fclose(file);
        return -1;
    }
    
    if (strcmp(role, "user") == 0) {
        fprintf(file, "\n## User:\n%s\n", escaped_content);
    } else if (strcmp(role, "assistant") == 0) {
        fprintf(file, "\n## Assistant:\n%s\n", escaped_content);
    }
    
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
        history->messages[i].role = NULL;
        history->messages[i].content = NULL;
    }
    
    free(history->messages);
    history->messages = NULL;
    history->count = 0;
    history->capacity = 0;
}