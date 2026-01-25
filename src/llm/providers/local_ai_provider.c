#include "llm_provider.h"
#include "api_common.h"
#include "streaming.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int local_ai_detect_provider(const char* api_url);
static char* local_ai_build_request_json(const LLMProvider* provider,
                                        const char* model,
                                        const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message,
                                        int max_tokens,
                                        const ToolRegistry* tools);
static int local_ai_build_headers(const LLMProvider* provider,
                                 const char* api_key,
                                 const char** headers,
                                 int max_headers);
static int local_ai_parse_response(const LLMProvider* provider,
                                  const char* json_response,
                                  ParsedResponse* result);
// Tool calling functions removed - now handled by ModelCapabilities

// Local AI provider implementation
static int local_ai_detect_provider(const char* api_url) {
    // Local AI is the fallback provider - anything that's not Anthropic or OpenAI
    // This should be checked LAST in the provider registry
    if (api_url == NULL) return 0;
    
    // Explicitly exclude known cloud providers
    if (strstr(api_url, "api.anthropic.com") != NULL ||
        strstr(api_url, "api.openai.com") != NULL ||
        strstr(api_url, "openai.azure.com") != NULL ||
        strstr(api_url, "api.groq.com") != NULL) {
        return 0;
    }
    
    // Everything else is considered local AI (including remote LM servers)
    return 1;
}

static char* local_ai_build_request_json(const LLMProvider* provider,
                                        const char* model,
                                        const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message,
                                        int max_tokens,
                                        const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }
    // Local AI typically follows OpenAI format - system prompt in messages array
    return build_json_payload_model_aware(model, system_prompt, conversation,
                                        user_message, provider->capabilities.max_tokens_param,
                                        max_tokens, tools, format_openai_message, 0);
}

static int local_ai_build_headers(const LLMProvider* provider,
                                 const char* api_key,
                                 const char** headers,
                                 int max_headers) {
    (void)provider; // Suppress unused parameter warning

    if (max_headers < 2) {
        return 0; // Cannot fit required headers
    }

    int count = 0;
    static _Thread_local char auth_header[512];
    static char content_type[] = "Content-Type: application/json";

    // Add authorization header if API key provided (some local servers require it)
    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[count++] = auth_header;
    }

    // Add content type header
    if (count < max_headers - 1) {
        headers[count++] = content_type;
    }

    return count;
}

static int local_ai_parse_response(const LLMProvider* provider,
                                  const char* json_response,
                                  ParsedResponse* result) {
    (void)provider; // Suppress unused parameter warning
    // Local AI typically follows OpenAI response format
    return parse_api_response(json_response, result);
}

// Tool calling implementation removed - now handled by ModelCapabilities

// =============================================================================
// Streaming Support (OpenAI-compatible format used by llama.cpp, LM Studio, etc.)
// =============================================================================

static int local_ai_supports_streaming(const LLMProvider* provider) {
    (void)provider;
    return 1; // Most local AI servers support OpenAI-compatible streaming
}

/**
 * Parse tool call deltas from streaming response (OpenAI format)
 */
static void local_ai_parse_tool_call_delta(StreamingContext* ctx, cJSON* tool_calls) {
    if (ctx == NULL || tool_calls == NULL || !cJSON_IsArray(tool_calls)) {
        return;
    }

    int array_size = cJSON_GetArraySize(tool_calls);
    for (int i = 0; i < array_size; i++) {
        cJSON* tool_call = cJSON_GetArrayItem(tool_calls, i);
        if (tool_call == NULL) continue;

        cJSON* index_item = cJSON_GetObjectItem(tool_call, "index");
        int index = (index_item && cJSON_IsNumber(index_item)) ? index_item->valueint : 0;

        cJSON* id = cJSON_GetObjectItem(tool_call, "id");
        cJSON* function = cJSON_GetObjectItem(tool_call, "function");

        if (function != NULL) {
            cJSON* name = cJSON_GetObjectItem(function, "name");

            if (id != NULL && cJSON_IsString(id) && name != NULL && cJSON_IsString(name)) {
                streaming_emit_tool_start(ctx, id->valuestring, name->valuestring);
            }

            cJSON* arguments = cJSON_GetObjectItem(function, "arguments");
            if (arguments != NULL && cJSON_IsString(arguments) && strlen(arguments->valuestring) > 0) {
                const char* tool_id = NULL;
                if (index < ctx->tool_use_count && ctx->tool_uses[index].id != NULL) {
                    tool_id = ctx->tool_uses[index].id;
                } else if (ctx->current_tool_index >= 0 && ctx->current_tool_index < ctx->tool_use_count) {
                    tool_id = ctx->tool_uses[ctx->current_tool_index].id;
                }

                if (tool_id != NULL) {
                    streaming_emit_tool_delta(ctx, tool_id, arguments->valuestring,
                                             strlen(arguments->valuestring));
                }
            }
        }
    }
}

/**
 * Parse SSE data line (OpenAI-compatible format)
 */
static int local_ai_parse_stream_event(const LLMProvider* provider,
                                       StreamingContext* ctx,
                                       const char* json_data,
                                       size_t len) {
    (void)provider;

    if (ctx == NULL || json_data == NULL || len == 0) {
        return -1;
    }

    if (len == 6 && memcmp(json_data, "[DONE]", 6) == 0) {
        return 0;
    }

    cJSON* root = cJSON_ParseWithLength(json_data, len);
    if (root == NULL) {
        return -1;
    }

    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices != NULL && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        if (choice != NULL) {
            cJSON* delta = cJSON_GetObjectItem(choice, "delta");

            if (delta != NULL) {
                cJSON* content = cJSON_GetObjectItem(delta, "content");
                if (content != NULL && cJSON_IsString(content) && content->valuestring != NULL) {
                    streaming_emit_text(ctx, content->valuestring, strlen(content->valuestring));
                }

                cJSON* tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                if (tool_calls != NULL) {
                    local_ai_parse_tool_call_delta(ctx, tool_calls);
                }
            }

            cJSON* finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
            if (finish_reason != NULL && cJSON_IsString(finish_reason)) {
                char* new_reason = strdup(finish_reason->valuestring);
                if (new_reason != NULL) {
                    free(ctx->stop_reason);
                    ctx->stop_reason = new_reason;
                }
            }
        }
    }

    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage != NULL) {
        cJSON* prompt_tokens = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON* completion_tokens = cJSON_GetObjectItem(usage, "completion_tokens");

        if (prompt_tokens != NULL && cJSON_IsNumber(prompt_tokens)) {
            ctx->input_tokens = prompt_tokens->valueint;
        }
        if (completion_tokens != NULL && cJSON_IsNumber(completion_tokens)) {
            ctx->output_tokens = completion_tokens->valueint;
        }
    }

    cJSON_Delete(root);
    return 0;
}

/**
 * Build streaming request JSON (OpenAI-compatible format)
 */
static char* local_ai_build_streaming_request_json(const LLMProvider* provider,
                                                   const char* model,
                                                   const char* system_prompt,
                                                   const ConversationHistory* conversation,
                                                   const char* user_message,
                                                   int max_tokens,
                                                   const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }

    char* base_json = build_json_payload_model_aware(model, system_prompt, conversation,
                                                     user_message, provider->capabilities.max_tokens_param,
                                                     max_tokens, tools, format_openai_message, 0);
    if (base_json == NULL) {
        return NULL;
    }

    cJSON* root = cJSON_Parse(base_json);
    free(base_json);

    if (root == NULL) {
        return NULL;
    }

    cJSON_AddBoolToObject(root, "stream", 1);

    // Note: stream_options may not be supported by all local servers, but it's harmless
    cJSON* stream_options = cJSON_CreateObject();
    if (stream_options != NULL) {
        cJSON_AddBoolToObject(stream_options, "include_usage", 1);
        cJSON_AddItemToObject(root, "stream_options", stream_options);
    }

    char* result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return result;
}

// Local AI provider instance
static LLMProvider local_ai_provider = {
    .capabilities = {
        .name = "Local AI",
        .max_tokens_param = "max_tokens",
        .supports_system_message = 1,
        .requires_version_header = 0,
        .auth_header_format = "Authorization: Bearer %s",
        .version_header = NULL
    },
    .detect_provider = local_ai_detect_provider,
    .build_request_json = local_ai_build_request_json,
    .build_headers = local_ai_build_headers,
    .parse_response = local_ai_parse_response,
    // Tool functions removed - now handled by ModelCapabilities
    .validate_conversation = NULL,  // No special validation needed
    // Streaming support (OpenAI-compatible format)
    .supports_streaming = local_ai_supports_streaming,
    .parse_stream_event = local_ai_parse_stream_event,
    .build_streaming_request_json = local_ai_build_streaming_request_json,
    .cleanup_stream_state = NULL  // Local AI doesn't use thread-local state
};

int register_local_ai_provider(ProviderRegistry* registry) {
    return register_provider(registry, &local_ai_provider);
}