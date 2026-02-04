#include "api_common.h"
#include <cJSON.h>
#include "../llm/model_capabilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../util/json_escape.h"

extern ModelRegistry* get_model_registry(void);

size_t calculate_json_payload_size(const char* model, const char* system_prompt,
                                  const ConversationHistory* conversation,
                                  const char* user_message, const ToolRegistry* tools) {
    if (model == NULL || conversation == NULL) {
        return 0;
    }

    size_t base_size = 200;
    size_t model_len = strlen(model);
    size_t user_msg_len = user_message ? strlen(user_message) * 2 + 50 : 0;
    size_t system_len = system_prompt ? strlen(system_prompt) * 2 + 50 : 0;
    size_t history_len = 0;

    for (size_t i = 0; i < conversation->count; i++) {
        if (conversation->data[i].role == NULL ||
            conversation->data[i].content == NULL) {
            continue;
        }
        // *2 for JSON escaping headroom, +100 for tool_call metadata fields
        size_t msg_size = strlen(conversation->data[i].role) +
                         strlen(conversation->data[i].content) * 2 + 100;

        if (history_len > SIZE_MAX - msg_size) {
            return SIZE_MAX;
        }
        history_len += msg_size;
    }

    size_t tools_len = 0;
    if (tools != NULL && tools->functions.count > 0) {
        tools_len = tools->functions.count * 500;
    }

    size_t total = base_size;
    if (total > SIZE_MAX - model_len) return SIZE_MAX;
    total += model_len;
    if (total > SIZE_MAX - user_msg_len) return SIZE_MAX;
    total += user_msg_len;
    if (total > SIZE_MAX - system_len) return SIZE_MAX;
    total += system_len;
    if (total > SIZE_MAX - history_len) return SIZE_MAX;
    total += history_len;
    if (total > SIZE_MAX - tools_len) return SIZE_MAX;
    total += tools_len;
    if (total > SIZE_MAX - 200) return SIZE_MAX;
    total += 200;

    return total;
}

int format_openai_message(char* buffer, size_t buffer_size,
                         const ConversationMessage* message,
                         int is_first_message) {
    char *message_json = NULL;

    if (message == NULL || message->role == NULL || message->content == NULL) {
        return -1;
    }

    if (strcmp(message->role, "tool") == 0 && message->tool_call_id != NULL) {
        cJSON* json = cJSON_CreateObject();
        if (!json) return -1;

        cJSON_AddStringToObject(json, "role", "tool");
        cJSON_AddStringToObject(json, "content", message->content);
        cJSON_AddStringToObject(json, "tool_call_id", message->tool_call_id);

        message_json = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
    } else if (strcmp(message->role, "assistant") == 0 &&
               strstr(message->content, "\"tool_calls\"") != NULL) {
        // Content is pre-formatted JSON from a prior assistant tool_calls response
        message_json = strdup(message->content);
    } else {
        cJSON* json = cJSON_CreateObject();
        if (json) {
            cJSON_AddStringToObject(json, "role", message->role);
            cJSON_AddStringToObject(json, "content", message->content);
            message_json = cJSON_PrintUnformatted(json);
            cJSON_Delete(json);
        }
    }

    if (message_json == NULL) return -1;

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

static char* build_simple_message_json(const char* role, const char* content) {
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) return NULL;
    cJSON_AddStringToObject(json, "role", role);
    cJSON_AddStringToObject(json, "content", content);
    char* result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

int format_anthropic_message(char* buffer, size_t buffer_size,
                            const ConversationMessage* message,
                            int is_first_message) {
    char *message_json = NULL;

    if (message == NULL || message->role == NULL || message->content == NULL) {
        return -1;
    }

    if (strcmp(message->role, "tool") == 0) {
        // Anthropic encodes tool results as user messages with tool_result content blocks
        cJSON* json = cJSON_CreateObject();
        if (json) {
            cJSON_AddStringToObject(json, "role", "user");

            cJSON* content_array = cJSON_CreateArray();
            if (content_array) {
                cJSON* tool_result = cJSON_CreateObject();
                if (tool_result) {
                    cJSON_AddStringToObject(tool_result, "type", "tool_result");
                    cJSON_AddStringToObject(tool_result, "content", message->content);

                    if (message->tool_call_id != NULL) {
                        cJSON_AddStringToObject(tool_result, "tool_use_id", message->tool_call_id);
                    }

                    cJSON_AddItemToArray(content_array, tool_result);
                }
                cJSON_AddItemToObject(json, "content", content_array);
            }

            message_json = cJSON_PrintUnformatted(json);
            cJSON_Delete(json);
        }
    } else if (strcmp(message->role, "assistant") == 0 &&
               strstr(message->content, "\"tool_use\"") != NULL) {
        // Raw Anthropic response: extract the content array from the full JSON envelope
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
                    message_json = build_simple_message_json(message->role, message->content);
                }
            } else {
                message_json = build_simple_message_json(message->role, message->content);
            }
        } else {
            message_json = build_simple_message_json(message->role, message->content);
        }
    } else {
        message_json = build_simple_message_json(message->role, message->content);
    }

    if (message_json == NULL) return -1;

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

    for (size_t i = 0; i < conversation->count; i++) {
        if (skip_system_in_history && strcmp(conversation->data[i].role, "system") == 0) {
            continue;
        }

        int written = formatter(current, remaining, &conversation->data[i], message_count == 0);
        if (written < 0) return -1;

        current += written;
        remaining -= written;
        message_count++;
    }

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

int build_anthropic_messages_json(char* buffer, size_t buffer_size,
                                 const char* system_prompt,
                                 const ConversationHistory* conversation,
                                 const char* user_message,
                                 MessageFormatter formatter,
                                 int skip_system_in_history) {
    return build_messages_json(buffer, buffer_size, system_prompt,
                              conversation, user_message, formatter,
                              skip_system_in_history);
}

char* build_json_payload_common(const char* model, const char* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message, const char* max_tokens_param,
                               int max_tokens, const ToolRegistry* tools,
                               MessageFormatter formatter,
                               int system_at_top_level) {
    if (model == NULL || conversation == NULL) {
        return NULL;
    }

    size_t total_size = calculate_json_payload_size(model, system_prompt, conversation, user_message, tools);
    if (total_size == 0 || total_size == SIZE_MAX) {
        return NULL;
    }
    char* json = malloc(total_size);
    if (json == NULL) return NULL;

    char* current = json;
    size_t remaining = total_size;

    int written = snprintf(current, remaining, "{\"model\": \"%s\", \"messages\": [", model);
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;

    // Anthropic places system prompt at the top level, not inside the messages array
    if (system_at_top_level) {
        written = build_anthropic_messages_json(current, remaining,
                                               NULL,
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

    written = snprintf(current, remaining, "]");
    if (written < 0 || written >= (int)remaining) {
        free(json);
        return NULL;
    }
    current += written;
    remaining -= written;

    if (system_at_top_level && system_prompt != NULL) {
        char* escaped_system = json_escape_string(system_prompt);
        if (escaped_system == NULL) {
            free(json);
            return NULL;
        }
        written = snprintf(current, remaining, ", \"system\": \"%s\"", escaped_system);
        free(escaped_system);
        if (written < 0 || written >= (int)remaining) {
            free(json);
            return NULL;
        }
        current += written;
        remaining -= written;
    }

    if (max_tokens > 0 && max_tokens_param != NULL) {
        written = snprintf(current, remaining, ", \"%s\": %d", max_tokens_param, max_tokens);
        if (written < 0 || written >= (int)remaining) {
            free(json);
            return NULL;
        }
        current += written;
        remaining -= written;
    }

    if (tools != NULL && tools->functions.count > 0 && model) {
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
    return build_json_payload_common(model, system_prompt, conversation, user_message,
                                    max_tokens_param, max_tokens, tools,
                                    formatter, system_at_top_level);
}
