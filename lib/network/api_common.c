#include "api_common.h"
#include <cJSON.h>
#include "../llm/model_capabilities.h"
#include "../session/conversation_tracker.h"
#include "../tools/tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../util/json_escape.h"

/* Thread-local pending images for the current API call */
#ifdef __STDC_NO_THREADS__
static const ImageAttachment *g_pending_images = NULL;
static size_t g_pending_image_count = 0;
#else
static _Thread_local const ImageAttachment *g_pending_images = NULL;
static _Thread_local size_t g_pending_image_count = 0;
#endif

void api_common_set_pending_images(const ImageAttachment *images, size_t count) {
    g_pending_images = images;
    g_pending_image_count = count;
}

void api_common_clear_pending_images(void) {
    g_pending_images = NULL;
    g_pending_image_count = 0;
}

static char *format_user_message_with_images(const char *text, int is_anthropic) {
    cJSON *msg = cJSON_CreateObject();
    if (msg == NULL) return NULL;

    cJSON_AddStringToObject(msg, "role", "user");

    cJSON *content = cJSON_CreateArray();
    if (content == NULL) { cJSON_Delete(msg); return NULL; }

    /* Text part */
    cJSON *text_part = cJSON_CreateObject();
    if (text_part == NULL) { cJSON_Delete(msg); cJSON_Delete(content); return NULL; }
    cJSON_AddStringToObject(text_part, "type", "text");
    cJSON_AddStringToObject(text_part, "text", text);
    cJSON_AddItemToArray(content, text_part);

    /* Image parts */
    for (size_t i = 0; i < g_pending_image_count; i++) {
        const ImageAttachment *img = &g_pending_images[i];
        cJSON *img_part = cJSON_CreateObject();
        if (img_part == NULL) continue;

        if (is_anthropic) {
            cJSON_AddStringToObject(img_part, "type", "image");
            cJSON *source = cJSON_CreateObject();
            if (source) {
                cJSON_AddStringToObject(source, "type", "base64");
                cJSON_AddStringToObject(source, "media_type", img->mime_type);
                cJSON_AddStringToObject(source, "data", img->base64_data);
                cJSON_AddItemToObject(img_part, "source", source);
            }
        } else {
            cJSON_AddStringToObject(img_part, "type", "image_url");
            cJSON *image_url = cJSON_CreateObject();
            if (image_url) {
                /* Build data URI: data:<mime>;base64,<data> */
                size_t uri_len = strlen(img->mime_type) + strlen(img->base64_data) + 16;
                char *data_uri = malloc(uri_len);
                if (data_uri) {
                    snprintf(data_uri, uri_len, "data:%s;base64,%s",
                             img->mime_type, img->base64_data);
                    cJSON_AddStringToObject(image_url, "url", data_uri);
                    free(data_uri);
                }
                cJSON_AddItemToObject(img_part, "image_url", image_url);
            }
        }

        cJSON_AddItemToArray(content, img_part);
    }

    cJSON_AddItemToObject(msg, "content", content);

    char *result = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    return result;
}

size_t calculate_messages_buffer_size(const SystemPromptParts* system_prompt,
                                     const ConversationHistory* conversation,
                                     const char* user_message) {
    if (conversation == NULL) {
        return 0;
    }

    size_t base_size = 200;
    size_t user_msg_len = user_message ? strlen(user_message) * 2 + 50 : 0;
    size_t system_len = 0;
    if (system_prompt != NULL) {
        if (system_prompt->base_prompt != NULL)
            system_len += strlen(system_prompt->base_prompt) * 2 + 100;
        if (system_prompt->dynamic_context != NULL)
            system_len += strlen(system_prompt->dynamic_context) * 2 + 100;
    }
    size_t history_len = 0;

    for (size_t i = 0; i < conversation->count; i++) {
        if (conversation->data[i].role == NULL ||
            conversation->data[i].content == NULL) {
            continue;
        }
        size_t msg_size = strlen(conversation->data[i].role) +
                         strlen(conversation->data[i].content) * 2 + 100;

        if (history_len > SIZE_MAX - msg_size) {
            return SIZE_MAX;
        }
        history_len += msg_size;
    }

    size_t image_len = 0;
    if (g_pending_images != NULL) {
        for (size_t i = 0; i < g_pending_image_count; i++) {
            image_len += strlen(g_pending_images[i].base64_data) + 256;
        }
    }

    size_t total = base_size;
    if (total > SIZE_MAX - user_msg_len) return SIZE_MAX;
    total += user_msg_len;
    if (total > SIZE_MAX - system_len) return SIZE_MAX;
    total += system_len;
    if (total > SIZE_MAX - history_len) return SIZE_MAX;
    total += history_len;
    if (total > SIZE_MAX - image_len) return SIZE_MAX;
    total += image_len;

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
                       const SystemPromptParts* system_prompt,
                       const ConversationHistory* conversation,
                       const char* user_message,
                       MessageFormatter formatter,
                       int skip_system_in_history) {
    char* current = buffer;
    size_t remaining = buffer_size;
    int message_count = 0;

    if (system_prompt != NULL && !skip_system_in_history) {
        /* Emit base system prompt as first message (stable, cacheable prefix) */
        if (system_prompt->base_prompt != NULL) {
            ConversationMessage sys_msg = {
                .role = "system",
                .content = (char*)system_prompt->base_prompt,
                .tool_call_id = NULL,
                .tool_name = NULL
            };

            int written = formatter(current, remaining, &sys_msg, message_count == 0);
            if (written < 0) return -1;

            current += written;
            remaining -= written;
            message_count++;
        }

        /* Emit dynamic context as second system message (changes per-request) */
        if (system_prompt->dynamic_context != NULL && strlen(system_prompt->dynamic_context) > 0) {
            ConversationMessage dyn_msg = {
                .role = "system",
                .content = (char*)system_prompt->dynamic_context,
                .tool_call_id = NULL,
                .tool_name = NULL
            };

            int written = formatter(current, remaining, &dyn_msg, message_count == 0);
            if (written < 0) return -1;

            current += written;
            remaining -= written;
            message_count++;
        }
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
        if (g_pending_images != NULL && g_pending_image_count > 0) {
            char *img_json = format_user_message_with_images(user_message, skip_system_in_history);
            if (img_json == NULL) return -1;

            int written = 0;
            if (message_count > 0) {
                written = snprintf(current, remaining, ", %s", img_json);
            } else {
                written = snprintf(current, remaining, "%s", img_json);
            }
            free(img_json);
            if (written < 0 || written >= (int)remaining) return -1;

            current += written;
            remaining -= written;
            message_count++;
        } else {
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
    }

    return current - buffer;
}

int build_anthropic_messages_json(char* buffer, size_t buffer_size,
                                 const SystemPromptParts* system_prompt,
                                 const ConversationHistory* conversation,
                                 const char* user_message,
                                 MessageFormatter formatter,
                                 int skip_system_in_history) {
    return build_messages_json(buffer, buffer_size, system_prompt,
                              conversation, user_message, formatter,
                              skip_system_in_history);
}

char* build_json_payload_common(const char* model, const SystemPromptParts* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message, const char* max_tokens_param,
                               int max_tokens, const ToolRegistry* tools,
                               MessageFormatter formatter,
                               int system_at_top_level) {
    if (model == NULL || conversation == NULL) {
        return NULL;
    }

    /* Build messages into a temporary buffer via formatters */
    const SystemPromptParts* msg_prompt = system_at_top_level ? NULL : system_prompt;
    size_t msg_buf_size = calculate_messages_buffer_size(msg_prompt, conversation, user_message);
    if (msg_buf_size == 0 || msg_buf_size == SIZE_MAX) return NULL;

    char* msg_buffer = malloc(msg_buf_size);
    if (msg_buffer == NULL) return NULL;

    int msg_len;
    if (system_at_top_level) {
        msg_len = build_anthropic_messages_json(msg_buffer, msg_buf_size,
                                               NULL, conversation, user_message,
                                               formatter, system_at_top_level);
    } else {
        msg_len = build_messages_json(msg_buffer, msg_buf_size,
                                     system_prompt, conversation, user_message,
                                     formatter, system_at_top_level);
    }
    if (msg_len < 0) { free(msg_buffer); return NULL; }

    /* Wrap formatter output in array brackets for cJSON_CreateRaw */
    char* messages_raw_str = malloc((size_t)msg_len + 3);
    if (messages_raw_str == NULL) { free(msg_buffer); return NULL; }
    snprintf(messages_raw_str, (size_t)msg_len + 3, "[%.*s]", msg_len, msg_buffer);
    free(msg_buffer);

    /* Build the root JSON object with cJSON */
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) { free(messages_raw_str); return NULL; }

    cJSON_AddStringToObject(root, "model", model);

    cJSON* messages_raw = cJSON_CreateRaw(messages_raw_str);
    free(messages_raw_str);
    if (messages_raw == NULL) { cJSON_Delete(root); return NULL; }
    cJSON_AddItemToObject(root, "messages", messages_raw);

    /* Anthropic: system prompt as array of content blocks with cache_control */
    if (system_at_top_level && system_prompt != NULL) {
        int has_base = system_prompt->base_prompt != NULL && strlen(system_prompt->base_prompt) > 0;
        int has_dynamic = system_prompt->dynamic_context != NULL && strlen(system_prompt->dynamic_context) > 0;

        if (has_base || has_dynamic) {
            cJSON* system_array = cJSON_CreateArray();
            if (system_array != NULL) {
                if (has_base) {
                    cJSON* base_block = cJSON_CreateObject();
                    if (base_block != NULL) {
                        cJSON_AddStringToObject(base_block, "type", "text");
                        cJSON_AddStringToObject(base_block, "text", system_prompt->base_prompt);
                        cJSON* cache_ctl = cJSON_CreateObject();
                        if (cache_ctl != NULL) {
                            cJSON_AddStringToObject(cache_ctl, "type", "ephemeral");
                            cJSON_AddItemToObject(base_block, "cache_control", cache_ctl);
                        }
                        cJSON_AddItemToArray(system_array, base_block);
                    }
                }

                if (has_dynamic) {
                    cJSON* dyn_block = cJSON_CreateObject();
                    if (dyn_block != NULL) {
                        cJSON_AddStringToObject(dyn_block, "type", "text");
                        cJSON_AddStringToObject(dyn_block, "text", system_prompt->dynamic_context);
                        cJSON_AddItemToArray(system_array, dyn_block);
                    }
                }

                cJSON_AddItemToObject(root, "system", system_array);
            }
        }
    }

    if (max_tokens > 0 && max_tokens_param != NULL) {
        cJSON_AddNumberToObject(root, max_tokens_param, max_tokens);
    }

    if (tools != NULL && tools->functions.count > 0 && model) {
        ModelRegistry* registry = get_model_registry();
        if (registry) {
            char* tools_json_str = generate_model_tools_json(registry, model, tools);
            if (tools_json_str != NULL) {
                cJSON* tools_arr = cJSON_Parse(tools_json_str);
                free(tools_json_str);

                if (tools_arr != NULL) {
                    /* For Anthropic, add cache_control on the last tool definition */
                    if (system_at_top_level && cJSON_IsArray(tools_arr)) {
                        int tool_count = cJSON_GetArraySize(tools_arr);
                        if (tool_count > 0) {
                            cJSON* last_tool = cJSON_GetArrayItem(tools_arr, tool_count - 1);
                            if (last_tool != NULL) {
                                cJSON* cache_ctl = cJSON_CreateObject();
                                if (cache_ctl != NULL) {
                                    cJSON_AddStringToObject(cache_ctl, "type", "ephemeral");
                                    cJSON_AddItemToObject(last_tool, "cache_control", cache_ctl);
                                }
                            }
                        }
                    }
                    cJSON_AddItemToObject(root, "tools", tools_arr);
                }
            }
        }
    }

    char* result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return result;
}

char* build_json_payload_model_aware(const char* model, const SystemPromptParts* system_prompt,
                                    const ConversationHistory* conversation,
                                    const char* user_message, const char* max_tokens_param,
                                    int max_tokens, const ToolRegistry* tools,
                                    MessageFormatter formatter,
                                    int system_at_top_level) {
    return build_json_payload_common(model, system_prompt, conversation, user_message,
                                    max_tokens_param, max_tokens, tools,
                                    formatter, system_at_top_level);
}
