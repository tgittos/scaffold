#include "llm_provider.h"
#include "api_common.h"
#include "streaming.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Thread-local state for tracking Anthropic content blocks during streaming
static _Thread_local char* current_block_type = NULL;
static _Thread_local char* current_tool_id = NULL;

// Forward declarations
static int anthropic_detect_provider(const char* api_url);
static char* anthropic_build_request_json(const LLMProvider* provider,
                                         const char* model,
                                         const char* system_prompt,
                                         const ConversationHistory* conversation,
                                         const char* user_message,
                                         int max_tokens,
                                         const ToolRegistry* tools);
static int anthropic_build_headers(const LLMProvider* provider,
                                  const char* api_key,
                                  const char** headers,
                                  int max_headers);
static int anthropic_parse_response(const LLMProvider* provider,
                                   const char* json_response,
                                   ParsedResponse* result);

// Anthropic provider implementation
static int anthropic_detect_provider(const char* api_url) {
    if (api_url == NULL) return 0;
    return strstr(api_url, "api.anthropic.com") != NULL;
}

static char* anthropic_build_request_json(const LLMProvider* provider,
                                         const char* model,
                                         const char* system_prompt,
                                         const ConversationHistory* conversation,
                                         const char* user_message,
                                         int max_tokens,
                                         const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }
    // Anthropic-specific request building - system prompt at top level
    // Use the specialized Anthropic message builder to handle tool_result validation
    return build_json_payload_model_aware(model, system_prompt, conversation,
                                        user_message, provider->capabilities.max_tokens_param,
                                        max_tokens, tools, format_anthropic_message, 1);
}

static int anthropic_build_headers(const LLMProvider* provider,
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
    static char version_header[] = "anthropic-version: 2023-06-01";

    // Add x-api-key header if API key provided
    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
        headers[count++] = auth_header;
    }

    // Add version header (required by Anthropic)
    if (count < max_headers - 1) {
        headers[count++] = version_header;
    }

    // Add content type header
    if (count < max_headers - 1) {
        headers[count++] = content_type;
    }

    return count;
}

static int anthropic_parse_response(const LLMProvider* provider,
                                   const char* json_response,
                                   ParsedResponse* result) {
    (void)provider; // Suppress unused parameter warning
    // Use existing Anthropic response parser
    return parse_anthropic_response(json_response, result);
}

// =============================================================================
// Streaming Support
// =============================================================================

static int anthropic_supports_streaming(const LLMProvider* provider) {
    (void)provider;
    return 1;  // Anthropic streaming is now supported
}

/**
 * Clean up thread-local streaming state
 * Called when stream is complete or on error
 */
static void anthropic_cleanup_stream_state_internal(void) {
    free(current_block_type);
    current_block_type = NULL;
    free(current_tool_id);
    current_tool_id = NULL;
}

/**
 * Provider interface function for cleanup
 */
static void anthropic_cleanup_stream_state(const LLMProvider* provider) {
    (void)provider;
    anthropic_cleanup_stream_state_internal();
}

/**
 * Parse a single SSE event from Anthropic streaming response
 *
 * Anthropic uses event types to distinguish between different message parts:
 * - message_start: Initial message metadata and input tokens
 * - content_block_start: Start of text, thinking, or tool_use block
 * - content_block_delta: Incremental content for the current block
 * - content_block_stop: End of current content block
 * - message_delta: Final metadata including stop_reason and output tokens
 * - message_stop: Stream complete
 * - ping: Heartbeat (ignored)
 * - error: Error from API
 *
 * The event type comes from ctx->current_event_type (parsed from "event:" line)
 */
static int anthropic_parse_stream_event(const LLMProvider* provider,
                                        StreamingContext* ctx,
                                        const char* json_data,
                                        size_t len) {
    (void)provider;

    if (ctx == NULL || json_data == NULL || len == 0) {
        return -1;
    }

    // Parse JSON
    cJSON* root = cJSON_ParseWithLength(json_data, len);
    if (root == NULL) {
        return -1;
    }

    // Get the type field from the JSON payload
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type == NULL || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return -1;
    }

    const char* type_str = type->valuestring;

    // Handle message_start - contains input tokens
    // Also clean up any stale state from previous streams
    if (strcmp(type_str, "message_start") == 0) {
        anthropic_cleanup_stream_state_internal();  // Ensure clean state for new stream

        cJSON* message = cJSON_GetObjectItem(root, "message");
        if (message != NULL) {
            cJSON* usage = cJSON_GetObjectItem(message, "usage");
            if (usage != NULL) {
                cJSON* input_tokens = cJSON_GetObjectItem(usage, "input_tokens");
                if (input_tokens != NULL && cJSON_IsNumber(input_tokens)) {
                    ctx->input_tokens = input_tokens->valueint;
                }
            }
        }
    }
    // Handle content_block_start - identifies block type
    else if (strcmp(type_str, "content_block_start") == 0) {
        cJSON* content_block = cJSON_GetObjectItem(root, "content_block");
        if (content_block != NULL) {
            cJSON* block_type = cJSON_GetObjectItem(content_block, "type");
            if (block_type != NULL && cJSON_IsString(block_type)) {
                // Store current block type for delta routing
                free(current_block_type);
                current_block_type = strdup(block_type->valuestring);

                // Handle tool_use block start
                if (strcmp(block_type->valuestring, "tool_use") == 0) {
                    cJSON* id = cJSON_GetObjectItem(content_block, "id");
                    cJSON* name = cJSON_GetObjectItem(content_block, "name");

                    if (id != NULL && cJSON_IsString(id) &&
                        name != NULL && cJSON_IsString(name)) {
                        // Store tool ID for delta routing
                        free(current_tool_id);
                        current_tool_id = strdup(id->valuestring);

                        streaming_emit_tool_start(ctx, id->valuestring, name->valuestring);
                    }
                }
            }
        }
    }
    // Handle content_block_delta - incremental content
    else if (strcmp(type_str, "content_block_delta") == 0) {
        cJSON* delta = cJSON_GetObjectItem(root, "delta");
        if (delta != NULL) {
            cJSON* delta_type = cJSON_GetObjectItem(delta, "type");
            if (delta_type != NULL && cJSON_IsString(delta_type)) {
                const char* delta_type_str = delta_type->valuestring;

                if (strcmp(delta_type_str, "text_delta") == 0) {
                    cJSON* text = cJSON_GetObjectItem(delta, "text");
                    if (text != NULL && cJSON_IsString(text) && text->valuestring != NULL) {
                        streaming_emit_text(ctx, text->valuestring, strlen(text->valuestring));
                    }
                }
                else if (strcmp(delta_type_str, "thinking_delta") == 0) {
                    cJSON* thinking = cJSON_GetObjectItem(delta, "thinking");
                    if (thinking != NULL && cJSON_IsString(thinking) && thinking->valuestring != NULL) {
                        streaming_emit_thinking(ctx, thinking->valuestring, strlen(thinking->valuestring));
                    }
                }
                else if (strcmp(delta_type_str, "input_json_delta") == 0) {
                    cJSON* partial_json = cJSON_GetObjectItem(delta, "partial_json");
                    if (partial_json != NULL && cJSON_IsString(partial_json) &&
                        partial_json->valuestring != NULL && current_tool_id != NULL) {
                        streaming_emit_tool_delta(ctx, current_tool_id,
                                                 partial_json->valuestring,
                                                 strlen(partial_json->valuestring));
                    }
                }
            }
        }
    }
    // Handle content_block_stop - end of current block
    else if (strcmp(type_str, "content_block_stop") == 0) {
        // Block complete - clear block type but keep tool_id for potential future blocks
        free(current_block_type);
        current_block_type = NULL;
    }
    // Handle message_delta - contains stop_reason and output tokens
    else if (strcmp(type_str, "message_delta") == 0) {
        cJSON* delta = cJSON_GetObjectItem(root, "delta");
        if (delta != NULL) {
            cJSON* stop_reason = cJSON_GetObjectItem(delta, "stop_reason");
            if (stop_reason != NULL && cJSON_IsString(stop_reason)) {
                free(ctx->stop_reason);
                ctx->stop_reason = strdup(stop_reason->valuestring);
            }
        }

        cJSON* usage = cJSON_GetObjectItem(root, "usage");
        if (usage != NULL) {
            cJSON* output_tokens = cJSON_GetObjectItem(usage, "output_tokens");
            if (output_tokens != NULL && cJSON_IsNumber(output_tokens)) {
                ctx->output_tokens = output_tokens->valueint;
            }
        }
    }
    // Handle message_stop - stream complete
    else if (strcmp(type_str, "message_stop") == 0) {
        streaming_emit_complete(ctx, ctx->stop_reason ? ctx->stop_reason : "end_turn");
        anthropic_cleanup_stream_state_internal();
    }
    // Handle error
    else if (strcmp(type_str, "error") == 0) {
        cJSON* error = cJSON_GetObjectItem(root, "error");
        if (error != NULL) {
            cJSON* message = cJSON_GetObjectItem(error, "message");
            if (message != NULL && cJSON_IsString(message)) {
                streaming_emit_error(ctx, message->valuestring);
            }
        }
        anthropic_cleanup_stream_state_internal();
    }
    // Handle ping - just ignore
    else if (strcmp(type_str, "ping") == 0) {
        // Heartbeat - no action needed
    }

    cJSON_Delete(root);
    return 0;
}

/**
 * Build Anthropic streaming request JSON
 *
 * Adds "stream": true to the request
 */
static char* anthropic_build_streaming_request_json(const LLMProvider* provider,
                                                     const char* model,
                                                     const char* system_prompt,
                                                     const ConversationHistory* conversation,
                                                     const char* user_message,
                                                     int max_tokens,
                                                     const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }

    // Build base request using the existing Anthropic message builder
    char* base_json = build_json_payload_model_aware(model, system_prompt, conversation,
                                                      user_message, provider->capabilities.max_tokens_param,
                                                      max_tokens, tools, format_anthropic_message, 1);
    if (base_json == NULL) {
        return NULL;
    }

    // Parse the JSON to add streaming parameter
    cJSON* root = cJSON_Parse(base_json);
    free(base_json);

    if (root == NULL) {
        return NULL;
    }

    // Add stream: true
    cJSON_AddBoolToObject(root, "stream", 1);

    // Convert back to string
    char* result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return result;
}

// Anthropic provider instance
static LLMProvider anthropic_provider = {
    .capabilities = {
        .name = "Anthropic",
        .max_tokens_param = "max_tokens",
        .supports_system_message = 1,
        .requires_version_header = 1,
        .auth_header_format = "x-api-key: %s",
        .version_header = "anthropic-version: 2023-06-01"
    },
    .detect_provider = anthropic_detect_provider,
    .build_request_json = anthropic_build_request_json,
    .build_headers = anthropic_build_headers,
    .parse_response = anthropic_parse_response,
    // Streaming support
    .supports_streaming = anthropic_supports_streaming,
    .parse_stream_event = anthropic_parse_stream_event,
    .build_streaming_request_json = anthropic_build_streaming_request_json,
    .cleanup_stream_state = anthropic_cleanup_stream_state
};

int register_anthropic_provider(ProviderRegistry* registry) {
    return register_provider(registry, &anthropic_provider);
}