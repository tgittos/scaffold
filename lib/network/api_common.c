#include "api_common.h"
#include <cJSON.h>
#include "../llm/model_capabilities.h"
#include "../session/conversation_tracker.h"
#include "../tools/tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../util/json_escape.h"

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

static char* build_simple_message_json(const char* role, const char* content) {
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) return NULL;
    cJSON_AddStringToObject(json, "role", role);
    cJSON_AddStringToObject(json, "content", content);
    char* result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

static char* summarize_tool_calls(const char* raw_json) {
    cJSON* root = cJSON_Parse(raw_json);
    if (!root) return NULL;

    cJSON* tool_calls = cJSON_GetObjectItem(root, "tool_calls");
    if (!tool_calls || !cJSON_IsArray(tool_calls)) {
        cJSON_Delete(root);
        return NULL;
    }

    // Build summary: "Calling tool1(arg1=\"val1\") tool2(...)"
    size_t buf_size = 256;
    char* summary = malloc(buf_size);
    if (!summary) { cJSON_Delete(root); return NULL; }
    size_t offset = 0;

    int count = cJSON_GetArraySize(tool_calls);
    for (int i = 0; i < count; i++) {
        cJSON* tc = cJSON_GetArrayItem(tool_calls, i);
        cJSON* fn = cJSON_GetObjectItem(tc, "function");
        if (!fn) continue;

        cJSON* name = cJSON_GetObjectItem(fn, "name");
        cJSON* args = cJSON_GetObjectItem(fn, "arguments");
        const char* name_str = name && cJSON_IsString(name) ? name->valuestring : "unknown";
        const char* args_str = args && cJSON_IsString(args) ? args->valuestring : "{}";

        // Parse arguments JSON to build arg summary
        cJSON* args_obj = cJSON_Parse(args_str);
        char arg_summary[512] = "";
        if (args_obj) {
            size_t aoff = 0;
            cJSON* field = NULL;
            int first = 1;
            cJSON_ArrayForEach(field, args_obj) {
                if (!field->string) continue;
                const char* val_str;
                char num_buf[64];
                if (cJSON_IsString(field)) {
                    val_str = field->valuestring;
                } else if (cJSON_IsNumber(field)) {
                    snprintf(num_buf, sizeof(num_buf), "%g", field->valuedouble);
                    val_str = num_buf;
                } else if (cJSON_IsBool(field)) {
                    val_str = cJSON_IsTrue(field) ? "true" : "false";
                } else {
                    val_str = "...";
                }
                int written = snprintf(arg_summary + aoff, sizeof(arg_summary) - aoff,
                                       "%s%s=\"%s\"", first ? "" : ", ", field->string, val_str);
                if (written < 0 || (size_t)written >= sizeof(arg_summary) - aoff) break;
                aoff += written;
                first = 0;
            }
            cJSON_Delete(args_obj);
        }

        // Ensure enough space
        size_t needed = strlen(name_str) + strlen(arg_summary) + 32;
        while (offset + needed >= buf_size) {
            buf_size *= 2;
            char* tmp = realloc(summary, buf_size);
            if (!tmp) { free(summary); cJSON_Delete(root); return NULL; }
            summary = tmp;
        }

        int written = snprintf(summary + offset, buf_size - offset,
                               "%sCalling %s(%s)",
                               (i > 0) ? "\n" : "", name_str, arg_summary);
        if (written > 0) offset += written;
    }

    cJSON_Delete(root);
    if (offset == 0) { free(summary); return NULL; }
    return summary;
}

int format_openai_message(char* buffer, size_t buffer_size,
                         const ConversationMessage* message,
                         int is_first_message) {
    char *message_json = NULL;

    if (message == NULL || message->role == NULL || message->content == NULL) {
        return -1;
    }

    if (strcmp(message->role, "tool") == 0) {
        // Rewrite tool results as system messages so the model treats them as
        // background context rather than user-provided conversational input
        const char* name = (message->tool_name != NULL) ? message->tool_name : "unknown";

        size_t prefix_len = strlen("[Tool  result]:\n") + strlen(name) + 1;
        size_t content_len = strlen(message->content);
        char* combined = malloc(prefix_len + content_len + 1);
        if (!combined) return -1;
        snprintf(combined, prefix_len + content_len + 1,
                 "[Tool %s result]:\n%s", name, message->content);

        cJSON* json = cJSON_CreateObject();
        if (!json) { free(combined); return -1; }
        cJSON_AddStringToObject(json, "role", "system");
        cJSON_AddStringToObject(json, "content", combined);
        message_json = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
        free(combined);
    } else if (strcmp(message->role, "assistant") == 0 &&
               strstr(message->content, "\"tool_calls\"") != NULL) {
        // Rewrite assistant tool_calls as plain text summaries so the API
        // doesn't reject orphaned tool_calls without matching tool responses
        char* summary = summarize_tool_calls(message->content);
        if (summary) {
            cJSON* json = cJSON_CreateObject();
            if (json) {
                cJSON_AddStringToObject(json, "role", "assistant");
                cJSON_AddStringToObject(json, "content", summary);
                message_json = cJSON_PrintUnformatted(json);
                cJSON_Delete(json);
            }
            free(summary);
        }
        // Fallback: if summarization fails, send as plain assistant text
        if (message_json == NULL) {
            message_json = build_simple_message_json("assistant", message->content);
        }
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
