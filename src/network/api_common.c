#include "api_common.h"
#include "json_utils.h"
#include "model_capabilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Model registry is now used for all tool handling
extern ModelRegistry* get_model_registry(void);

// Use unified JSON escaping from json_utils.h

size_t calculate_json_payload_size(const char* model, const char* system_prompt,
                                  const ConversationHistory* conversation,
                                  const char* user_message, const ToolRegistry* tools) {
    size_t base_size = 200; // Base JSON structure
    size_t model_len = strlen(model);
    size_t user_msg_len = user_message ? strlen(user_message) * 2 + 50 : 0;
    size_t system_len = system_prompt ? strlen(system_prompt) * 2 + 50 : 0;
    size_t history_len = 0;
    
    // Calculate space needed for conversation history
    for (int i = 0; i < conversation->count; i++) {
        history_len += strlen(conversation->messages[i].role) + 
                      strlen(conversation->messages[i].content) * 2 + 100; // Extra space for tool metadata
    }
    
    // Account for tools JSON size
    size_t tools_len = 0;
    if (tools != NULL && tools->function_count > 0) {
        tools_len = tools->function_count * 500; // Rough estimate per tool
    }
    
    return base_size + model_len + user_msg_len + system_len + history_len + tools_len + 200;
}

int format_openai_message(char* buffer, size_t buffer_size,
                         const ConversationMessage* message,
                         int is_first_message) {
    char *message_json = NULL;
    
    // Validate message fields first
    if (message == NULL || message->role == NULL || message->content == NULL) {
        return -1;
    }
    
    if (strcmp(message->role, "tool") == 0 && message->tool_call_id != NULL) {
        // Build tool message
        JsonBuilder builder = {0};
        if (json_builder_init(&builder) != 0) return -1;
        
        json_builder_start_object(&builder);
        json_builder_add_string(&builder, "role", "tool");
        json_builder_add_separator(&builder);
        json_builder_add_string(&builder, "content", message->content);
        json_builder_add_separator(&builder);
        json_builder_add_string(&builder, "tool_call_id", message->tool_call_id);
        json_builder_end_object(&builder);
        
        message_json = json_builder_finalize(&builder);
        json_builder_cleanup(&builder);
    } else if (strcmp(message->role, "assistant") == 0 && 
               strstr(message->content, "\"tool_calls\"") != NULL) {
        // This is an assistant message with tool calls - use the content as-is (it's already JSON)
        message_json = strdup(message->content);
    } else {
        // Regular message
        message_json = json_build_message(message->role, message->content);
    }
    
    if (message_json == NULL) return -1;
    
    // Build final result with optional comma separator
    int written = 0;
    if (!is_first_message) {
        written = snprintf(buffer, buffer_size, ", %s", message_json);
    } else {
        written = snprintf(buffer, buffer_size, "%s", message_json);
    }
    
    free(message_json);
    
    if (written < 0 || written >= (int)buffer_size) {
        return -1;
    }
    
    return written;
}

int format_anthropic_message(char* buffer, size_t buffer_size,
                            const ConversationMessage* message,
                            int is_first_message) {
    char *message_json = NULL;
    JsonBuilder builder = {0};
    
    // Validate message fields first
    if (message == NULL || message->role == NULL || message->content == NULL) {
        return -1;
    }
    
    if (strcmp(message->role, "tool") == 0) {
        // Tool results in Anthropic are user messages with tool_result content
        if (json_builder_init(&builder) != 0) return -1;
        
        json_builder_start_object(&builder);
        json_builder_add_string(&builder, "role", "user");
        json_builder_add_separator(&builder);
        json_builder_add_object(&builder, "content", NULL);
        
        // Build content array manually for complex structure
        json_builder_cleanup(&builder);
        
        if (message->tool_call_id != NULL) {
            message_json = malloc(strlen(message->content) * 2 + 200);
            if (message_json) {
                char *escaped_content = json_escape_string(message->content);
                snprintf(message_json, strlen(message->content) * 2 + 200,
                        "{\"role\": \"user\", \"content\": [{\"type\": \"tool_result\", "
                        "\"tool_use_id\": \"%s\", \"content\": \"%s\"}]}",
                        message->tool_call_id, escaped_content ? escaped_content : "");
                free(escaped_content);
            }
        } else {
            message_json = malloc(strlen(message->content) * 2 + 150);
            if (message_json) {
                char *escaped_content = json_escape_string(message->content);
                snprintf(message_json, strlen(message->content) * 2 + 150,
                        "{\"role\": \"user\", \"content\": [{\"type\": \"tool_result\", "
                        "\"content\": \"%s\"}]}",
                        escaped_content ? escaped_content : "");
                free(escaped_content);
            }
        }
    } else if (strcmp(message->role, "assistant") == 0 && 
               strstr(message->content, "\"tool_use\"") != NULL) {
        // This is a raw Anthropic response with tool_use blocks - extract the content
        const char *content_start = strstr(message->content, "\"content\":");
        if (content_start) {
            const char *array_start = strchr(content_start, '[');
            if (array_start) {
                const char *array_end = strrchr(message->content, ']');
                if (array_end && array_end > array_start) {
                    int content_len = array_end - array_start + 1;
                    message_json = malloc(content_len + 50);
                    if (message_json) {
                        snprintf(message_json, content_len + 50,
                                "{\"role\": \"assistant\", \"content\": %.*s}",
                                content_len, array_start);
                    }
                } else {
                    // Fallback to escaped content
                    message_json = json_build_message(message->role, message->content);
                }
            } else {
                // Fallback to escaped content
                message_json = json_build_message(message->role, message->content);
            }
        } else {
            // Fallback to escaped content
            message_json = json_build_message(message->role, message->content);
        }
    } else {
        // Regular message
        message_json = json_build_message(message->role, message->content);
    }
    
    if (message_json == NULL) return -1;
    
    // Build final result with optional comma separator
    int written = 0;
    if (!is_first_message) {
        written = snprintf(buffer, buffer_size, ", %s", message_json);
    } else {
        written = snprintf(buffer, buffer_size, "%s", message_json);
    }
    
    free(message_json);
    
    if (written < 0 || written >= (int)buffer_size) {
        return -1;
    }
    
    return written;
}

int build_messages_json(char* buffer, size_t buffer_size,
                       const char* system_prompt,
                       const ConversationHistory* conversation,
                       const char* user_message,
                       MessageFormatter formatter,
                       int skip_system_in_history) {
    char* current = buffer;
    size_t remaining = buffer_size;
    int message_count = 0;
    
    // Add system prompt if available and not skipping
    if (system_prompt != NULL && !skip_system_in_history) {
        ConversationMessage sys_msg = {
            .role = "system",
            .content = (char*)system_prompt,
            .tool_call_id = NULL,
            .tool_name = NULL
        };
        
        int written = formatter(current, remaining, &sys_msg, 1);
        if (written < 0) return -1;
        
        current += written;
        remaining -= written;
        message_count++;
    }
    
    // Add conversation history
    for (int i = 0; i < conversation->count; i++) {
        // Skip system messages if requested
        if (skip_system_in_history && strcmp(conversation->messages[i].role, "system") == 0) {
            continue;
        }
        
        int written = formatter(current, remaining, &conversation->messages[i], message_count == 0);
        if (written < 0) return -1;
        
        current += written;
        remaining -= written;
        message_count++;
    }
    
    // Add current user message if provided
    if (user_message != NULL && strlen(user_message) > 0) {
        ConversationMessage user_msg = {
            .role = "user",
            .content = (char*)user_message,
            .tool_call_id = NULL,
            .tool_name = NULL
        };
        
        int written = formatter(current, remaining, &user_msg, message_count == 0);
        if (written < 0) return -1;
        
        current += written;
        remaining -= written;
        message_count++;
    }
    
    return current - buffer;
}

// Removed message_contains_tool_use_id function - no longer needed after removing orphaned tool filtering

// Anthropic-specific message builder that ensures tool_result messages 
// have corresponding tool_use blocks in the previous message
int build_anthropic_messages_json(char* buffer, size_t buffer_size,
                                 const char* system_prompt,
                                 const ConversationHistory* conversation,
                                 const char* user_message,
                                 MessageFormatter formatter,
                                 int skip_system_in_history) {
    char* current = buffer;
    size_t remaining = buffer_size;
    int message_count = 0;
    
    // Add system prompt if available and not skipping
    if (system_prompt != NULL && !skip_system_in_history) {
        ConversationMessage sys_msg = {
            .role = "system",
            .content = (char*)system_prompt,
            .tool_call_id = NULL,
            .tool_name = NULL
        };
        
        int written = formatter(current, remaining, &sys_msg, 1);
        if (written < 0) return -1;
        
        current += written;
        remaining -= written;
        message_count++;
    }
    
    // Add conversation history with Anthropic tool call validation
    for (int i = 0; i < conversation->count; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        
        // Skip system messages if requested
        if (skip_system_in_history && strcmp(msg->role, "system") == 0) {
            continue;
        }
        
        // NOTE: Removed orphaned tool result filtering - it was causing infinite loops
        // The AI agent needs to see ALL tool results to avoid re-requesting the same tools
        
        int written = formatter(current, remaining, msg, message_count == 0);
        if (written < 0) return -1;
        
        current += written;
        remaining -= written;
        message_count++;
    }
    
    // Add current user message if provided
    if (user_message != NULL && strlen(user_message) > 0) {
        ConversationMessage user_msg = {
            .role = "user",
            .content = (char*)user_message,
            .tool_call_id = NULL,
            .tool_name = NULL
        };
        
        int written = formatter(current, remaining, &user_msg, message_count == 0);
        if (written < 0) return -1;
        
        current += written;
        remaining -= written;
        message_count++;
    }
    
    return current - buffer;
}

char* build_json_payload_common(const char* model, const char* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message, const char* max_tokens_param,
                               int max_tokens, const ToolRegistry* tools,
                               MessageFormatter formatter,
                               int system_at_top_level) {
    // Calculate required buffer size
    size_t total_size = calculate_json_payload_size(model, system_prompt, conversation, user_message, tools);
    char* json = malloc(total_size);
    if (json == NULL) return NULL;
    
    // Start building JSON
    char* current = json;
    size_t remaining = total_size;
    
    int written = snprintf(current, remaining, "{\"model\": \"%s\", \"messages\": [", model);
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;
    
    // Build messages array - use Anthropic-specific builder if system_at_top_level (Anthropic API)
    if (system_at_top_level) {
        written = build_anthropic_messages_json(current, remaining, 
                                               NULL,  // system prompt handled at top level
                                               conversation, user_message, formatter,
                                               system_at_top_level);
    } else {
        written = build_messages_json(current, remaining, 
                                     system_prompt,
                                     conversation, user_message, formatter,
                                     system_at_top_level);
    }
    if (written < 0) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;
    
    // Close messages array
    written = snprintf(current, remaining, "]");
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;
    
    // Add system prompt at top level if requested (Anthropic style)
    if (system_at_top_level && system_prompt != NULL) {
        char* escaped_system = json_escape_string(system_prompt);
        if (escaped_system != NULL) {
            written = snprintf(current, remaining, ", \"system\": \"%s\"", escaped_system);
            free(escaped_system);
            if (written < 0 || written >= (int)remaining) {
                free(json);
                return NULL;
            }
            current += written;
            remaining -= written;
        }
    }
    
    // Add max_tokens if specified
    if (max_tokens > 0 && max_tokens_param != NULL) {
        written = snprintf(current, remaining, ", \"%s\": %d", max_tokens_param, max_tokens);
        if (written < 0 || written >= (int)remaining) {
            free(json);
            return NULL;
        }
        current += written;
        remaining -= written;
    }
    
    // Add tools if available - always use model capabilities
    if (tools != NULL && tools->function_count > 0 && model) {
        ModelRegistry* registry = get_model_registry();
        if (registry) {
            char* tools_json = generate_model_tools_json(registry, model, tools);
            if (tools_json != NULL) {
                written = snprintf(current, remaining, ", \"tools\": %s", tools_json);
                free(tools_json);
                if (written < 0 || written >= (int)remaining) {
                    free(json);
                    return NULL;
                }
                current += written;
                remaining -= written;
            }
        }
    }
    
    // Close JSON
    written = snprintf(current, remaining, "}");
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    
    return json;
}

char* build_json_payload_model_aware(const char* model, const char* system_prompt,
                                    const ConversationHistory* conversation,
                                    const char* user_message, const char* max_tokens_param,
                                    int max_tokens, const ToolRegistry* tools,
                                    MessageFormatter formatter,
                                    int system_at_top_level) {
    // Calculate required buffer size
    size_t total_size = calculate_json_payload_size(model, system_prompt, conversation, user_message, tools);
    char* json = malloc(total_size);
    if (json == NULL) return NULL;
    
    // Start building JSON
    char* current = json;
    size_t remaining = total_size;
    
    int written = snprintf(current, remaining, "{\"model\": \"%s\", \"messages\": [", model);
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;
    
    // Build messages array - use Anthropic-specific builder if system_at_top_level (Anthropic API)
    if (system_at_top_level) {
        written = build_anthropic_messages_json(current, remaining, 
                                               NULL,  // system prompt handled at top level
                                               conversation, user_message, formatter,
                                               system_at_top_level);
    } else {
        written = build_messages_json(current, remaining, 
                                     system_prompt,
                                     conversation, user_message, formatter,
                                     system_at_top_level);
    }
    if (written < 0) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;
    
    // Close messages array
    written = snprintf(current, remaining, "]");
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;
    
    // Add system prompt at top level if requested (Anthropic style)
    if (system_at_top_level && system_prompt != NULL) {
        char* escaped_system = json_escape_string(system_prompt);
        if (escaped_system != NULL) {
            written = snprintf(current, remaining, ", \"system\": \"%s\"", escaped_system);
            free(escaped_system);
            if (written < 0 || written >= (int)remaining) {
                free(json);
                return NULL;
            }
            current += written;
            remaining -= written;
        }
    }
    
    // Add max_tokens if specified
    if (max_tokens > 0 && max_tokens_param != NULL) {
        written = snprintf(current, remaining, ", \"%s\": %d", max_tokens_param, max_tokens);
        if (written < 0 || written >= (int)remaining) {
            free(json);
            return NULL;
        }
        current += written;
        remaining -= written;
    }
    
    // Add tools if available - use model-specific formatting
    if (tools != NULL && tools->function_count > 0) {
        char* tools_json = NULL;
        
        // Always use model-specific tool formatting
        ModelRegistry* registry = get_model_registry();
        if (registry && model) {
            tools_json = generate_model_tools_json(registry, model, tools);
        }
        
        if (tools_json != NULL) {
            written = snprintf(current, remaining, ", \"tools\": %s", tools_json);
            free(tools_json);
            if (written < 0 || written >= (int)remaining) {
                free(json);
                return NULL;
            }
            current += written;
            remaining -= written;
        }
    }
    
    // Close JSON
    written = snprintf(current, remaining, "}");
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    
    return json;
}