#include "../llm_provider.h"
#include "../../network/api_common.h"
#include "../../network/streaming.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int openai_detect_provider(const char* api_url);
static char* openai_build_request_json(const LLMProvider* provider,
                                      const char* model,
                                      const char* system_prompt,
                                      const ConversationHistory* conversation,
                                      const char* user_message,
                                      int max_tokens,
                                      const ToolRegistry* tools);
static int openai_build_headers(const LLMProvider* provider,
                               const char* api_key,
                               const char** headers,
                               int max_headers);
static int openai_parse_response(const LLMProvider* provider,
                                const char* json_response,
                                ParsedResponse* result);
// Tool calling functions removed - now handled by ModelCapabilities

// OpenAI provider implementation
static int openai_detect_provider(const char* api_url) {
    if (api_url == NULL) return 0;
    return strstr(api_url, "api.openai.com") != NULL ||
           strstr(api_url, "openai.azure.com") != NULL ||  // Support Azure OpenAI
           strstr(api_url, "api.groq.com") != NULL;        // Support Groq (OpenAI-compatible)
}

static char* openai_build_request_json(const LLMProvider* provider,
                                      const char* model,
                                      const char* system_prompt,
                                      const ConversationHistory* conversation,
                                      const char* user_message,
                                      int max_tokens,
                                      const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }
    // OpenAI-specific request building - system prompt in messages array
    return build_json_payload_model_aware(model, system_prompt, conversation,
                                        user_message, provider->capabilities.max_tokens_param,
                                        max_tokens, tools, format_openai_message, 0);
}

static int openai_build_headers(const LLMProvider* provider,
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

    // Add authorization header if API key provided and non-empty
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

static int openai_parse_response(const LLMProvider* provider,
                                const char* json_response,
                                ParsedResponse* result) {
    (void)provider; // Suppress unused parameter warning
    // Use existing OpenAI response parser
    return parse_api_response(json_response, result);
}

// Tool calling implementation removed - now handled by ModelCapabilities

// =============================================================================
// Streaming Support
// =============================================================================

static int openai_supports_streaming(const LLMProvider* provider) {
    (void)provider; // Suppress unused parameter warning
    return 1; // OpenAI supports streaming
}

/**
 * Parse tool call deltas from OpenAI streaming response
 *
 * OpenAI streams tool calls as deltas:
 * - First chunk has: index, id, type, function.name, function.arguments=""
 * - Subsequent chunks have: index, function.arguments="<partial>"
 */
static void openai_parse_tool_call_delta(StreamingContext* ctx, cJSON* tool_calls) {
    if (ctx == NULL || tool_calls == NULL || !cJSON_IsArray(tool_calls)) {
        return;
    }

    int array_size = cJSON_GetArraySize(tool_calls);
    for (int i = 0; i < array_size; i++) {
        cJSON* tool_call = cJSON_GetArrayItem(tool_calls, i);
        if (tool_call == NULL) {
            continue;
        }

        // Get index for this tool call
        cJSON* index_item = cJSON_GetObjectItem(tool_call, "index");
        int index = (index_item && cJSON_IsNumber(index_item)) ? index_item->valueint : 0;

        // Check if this is a new tool call (has id and name)
        cJSON* id = cJSON_GetObjectItem(tool_call, "id");
        cJSON* function = cJSON_GetObjectItem(tool_call, "function");

        if (function != NULL) {
            cJSON* name = cJSON_GetObjectItem(function, "name");

            // New tool call - has both id and name
            if (id != NULL && cJSON_IsString(id) && name != NULL && cJSON_IsString(name)) {
                streaming_emit_tool_start(ctx, id->valuestring, name->valuestring);
            }

            // Argument delta
            cJSON* arguments = cJSON_GetObjectItem(function, "arguments");
            if (arguments != NULL && cJSON_IsString(arguments) && strlen(arguments->valuestring) > 0) {
                // Find the tool by index
                const char* tool_id = NULL;
                if ((size_t)index < ctx->tool_uses.count && ctx->tool_uses.data[index].id != NULL) {
                    tool_id = ctx->tool_uses.data[index].id;
                } else if (ctx->current_tool_index >= 0 && (size_t)ctx->current_tool_index < ctx->tool_uses.count) {
                    tool_id = ctx->tool_uses.data[ctx->current_tool_index].id;
                }

                if (tool_id != NULL) {
                    streaming_emit_tool_delta(ctx, tool_id,
                                             arguments->valuestring,
                                             strlen(arguments->valuestring));
                }
            }
        }
    }
}

/**
 * Parse a single SSE data line from OpenAI streaming response
 *
 * OpenAI streams JSON objects with format:
 * - Text: {"choices":[{"delta":{"content":"Hello"}}]}
 * - Tool: {"choices":[{"delta":{"tool_calls":[...]}}]}
 * - Done: {"choices":[{"finish_reason":"stop"}]}
 * - Usage: {"usage":{"prompt_tokens":N,"completion_tokens":M}}
 *
 * Returns 0 on success, -1 on error
 */
static int openai_parse_stream_event(const LLMProvider* provider,
                                     StreamingContext* ctx,
                                     const char* json_data,
                                     size_t len) {
    (void)provider; // Suppress unused parameter warning

    if (ctx == NULL || json_data == NULL || len == 0) {
        return -1;
    }

    // Check for [DONE] signal
    if (len == 6 && memcmp(json_data, "[DONE]", 6) == 0) {
        return 0; // Stream complete signal - already handled by SSE parser
    }

    // Parse JSON
    cJSON* root = cJSON_ParseWithLength(json_data, len);
    if (root == NULL) {
        return -1;
    }

    // Extract choices array
    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices != NULL && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        if (choice != NULL) {
            cJSON* delta = cJSON_GetObjectItem(choice, "delta");

            if (delta != NULL) {
                // Text content
                cJSON* content = cJSON_GetObjectItem(delta, "content");
                if (content != NULL && cJSON_IsString(content) && content->valuestring != NULL) {
                    streaming_emit_text(ctx, content->valuestring, strlen(content->valuestring));
                }

                // Tool calls
                cJSON* tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
                if (tool_calls != NULL) {
                    openai_parse_tool_call_delta(ctx, tool_calls);
                }
            }

            // Finish reason
            cJSON* finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
            if (finish_reason != NULL && cJSON_IsString(finish_reason)) {
                // Store stop reason for later emission
                free(ctx->stop_reason);
                ctx->stop_reason = strdup(finish_reason->valuestring);
            }
        }
    }

    // Usage statistics (appears in final message with stream_options.include_usage)
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
 * Build OpenAI streaming request JSON
 *
 * Adds "stream": true and "stream_options": {"include_usage": true} to the request
 */
static char* openai_build_streaming_request_json(const LLMProvider* provider,
                                                  const char* model,
                                                  const char* system_prompt,
                                                  const ConversationHistory* conversation,
                                                  const char* user_message,
                                                  int max_tokens,
                                                  const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }

    // Build base request using model-aware function
    char* base_json = build_json_payload_model_aware(model, system_prompt, conversation,
                                                     user_message, provider->capabilities.max_tokens_param,
                                                     max_tokens, tools, format_openai_message, 0);
    if (base_json == NULL) {
        return NULL;
    }

    // Parse the JSON to add streaming parameters
    cJSON* root = cJSON_Parse(base_json);
    free(base_json);

    if (root == NULL) {
        return NULL;
    }

    // Add stream: true
    cJSON_AddBoolToObject(root, "stream", 1);

    // Add stream_options with include_usage: true
    cJSON* stream_options = cJSON_CreateObject();
    if (stream_options != NULL) {
        cJSON_AddBoolToObject(stream_options, "include_usage", 1);
        cJSON_AddItemToObject(root, "stream_options", stream_options);
    }

    // Convert back to string
    char* result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return result;
}

// OpenAI provider instance
static LLMProvider openai_provider = {
    .capabilities = {
        .name = "OpenAI",
        .max_tokens_param = "max_completion_tokens",
        .supports_system_message = 1,
        .requires_version_header = 0,
        .auth_header_format = "Authorization: Bearer %s",
        .version_header = NULL
    },
    .detect_provider = openai_detect_provider,
    .build_request_json = openai_build_request_json,
    .build_headers = openai_build_headers,
    .parse_response = openai_parse_response,
    // Streaming support
    .supports_streaming = openai_supports_streaming,
    .parse_stream_event = openai_parse_stream_event,
    .build_streaming_request_json = openai_build_streaming_request_json,
    .cleanup_stream_state = NULL  // OpenAI doesn't use thread-local state
};

int register_openai_provider(ProviderRegistry* registry) {
    return register_provider(registry, &openai_provider);
}