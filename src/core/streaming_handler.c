#include "streaming_handler.h"
#include "context_enhancement.h"
#include "tool_executor.h"
#include "streaming.h"
#include "llm_provider.h"
#include "output_formatter.h"
#include "json_output.h"
#include "debug_output.h"
#include "http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// =============================================================================
// Provider Registry Management
// =============================================================================

// Global provider registry for streaming support
static ProviderRegistry* g_provider_registry = NULL;

ProviderRegistry* streaming_get_provider_registry(void) {
    if (g_provider_registry == NULL) {
        g_provider_registry = malloc(sizeof(ProviderRegistry));
        if (g_provider_registry != NULL) {
            if (init_provider_registry(g_provider_registry) == 0) {
                register_openai_provider(g_provider_registry);
                register_anthropic_provider(g_provider_registry);
                register_local_ai_provider(g_provider_registry);
            }
        }
    }
    return g_provider_registry;
}

void streaming_handler_cleanup(void) {
    if (g_provider_registry != NULL) {
        cleanup_provider_registry(g_provider_registry);
        free(g_provider_registry);
        g_provider_registry = NULL;
    }
}

// =============================================================================
// Streaming Callback Infrastructure
// =============================================================================

// User data for SSE callbacks (holds context and provider for parsing)
typedef struct {
    StreamingContext* ctx;
    LLMProvider* provider;
} StreamingSSEUserData;

// Callback: SSE data event - parse with provider-specific parser
static void streaming_sse_data_callback(const char* data, size_t len, void* user_data) {
    if (data == NULL || len == 0 || user_data == NULL) {
        return;
    }

    StreamingSSEUserData* sse_data = (StreamingSSEUserData*)user_data;
    if (sse_data->ctx == NULL || sse_data->provider == NULL) {
        return;
    }

    if (sse_data->provider->parse_stream_event != NULL) {
        sse_data->provider->parse_stream_event(sse_data->provider, sse_data->ctx, data, len);
    }
}

// Callback: display text chunks as they arrive
static void streaming_text_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    display_streaming_text(text, len);
}

// Callback: display thinking chunks
static void streaming_thinking_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    display_streaming_thinking(text, len);
}

// Callback: display tool use start
static void streaming_tool_start_callback(const char* id, const char* name, void* user_data) {
    (void)id;
    (void)user_data;
    display_streaming_tool_start(name);
}

// Callback: stream end
static void streaming_end_callback(const char* stop_reason, void* user_data) {
    (void)stop_reason;
    (void)user_data;
    // Completion display is handled after we have token counts
}

// Callback: stream error
static void streaming_error_callback(const char* error, void* user_data) {
    (void)user_data;
    display_streaming_error(error);
}

// HTTP streaming callback that processes SSE chunks
static size_t stream_http_callback(const char* data, size_t size, void* user_data) {
    if (data == NULL || size == 0 || user_data == NULL) {
        return 0;
    }

    StreamingContext* ctx = (StreamingContext*)user_data;

    // Process the chunk through SSE parser
    // Provider-specific parsing happens via the on_sse_data callback
    if (streaming_process_chunk(ctx, data, size) != 0) {
        return 0;
    }

    return size;
}

// =============================================================================
// Main Streaming Message Processing
// =============================================================================

int streaming_process_message(RalphSession* session, const char* user_message,
                              int max_tokens, const char** headers) {
    if (session == NULL || user_message == NULL) {
        return -1;
    }

    // Get provider for this session
    ProviderRegistry* registry = streaming_get_provider_registry();
    if (registry == NULL) {
        fprintf(stderr, "Error: Failed to get provider registry\n");
        return -1;
    }

    LLMProvider* provider = detect_provider_for_url(registry, session->session_data.config.api_url);
    if (provider == NULL) {
        fprintf(stderr, "Error: No provider found for URL: %s\n", session->session_data.config.api_url);
        return -1;
    }

    // Build enhanced prompt with context
    char* final_prompt = build_enhanced_prompt_with_context(session, user_message);
    if (final_prompt == NULL) {
        return -1;
    }

    // Build streaming request JSON
    char* post_data = provider->build_streaming_request_json(
        provider,
        session->session_data.config.model,
        final_prompt,
        &session->session_data.conversation,
        user_message,
        max_tokens,
        &session->tools
    );

    free(final_prompt);

    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build streaming JSON payload\n");
        return -1;
    }

    debug_printf("Streaming POST data: %s\n\n", post_data);

    // Create streaming context with display callbacks
    StreamingContext* ctx = streaming_context_create();
    if (ctx == NULL) {
        free(post_data);
        fprintf(stderr, "Error: Failed to create streaming context\n");
        return -1;
    }

    // Set up user data for SSE callbacks (provider parsing)
    StreamingSSEUserData sse_user_data = {
        .ctx = ctx,
        .provider = provider
    };
    ctx->user_data = &sse_user_data;

    // Set up callbacks for real-time display and SSE data parsing
    ctx->on_text_chunk = streaming_text_callback;
    ctx->on_thinking_chunk = streaming_thinking_callback;
    ctx->on_tool_use_start = streaming_tool_start_callback;
    ctx->on_stream_end = streaming_end_callback;
    ctx->on_error = streaming_error_callback;
    ctx->on_sse_data = streaming_sse_data_callback;

    // Initialize streaming display
    display_streaming_init();

    // Configure streaming HTTP request
    struct StreamingHTTPConfig streaming_config = {
        .base = DEFAULT_HTTP_CONFIG,
        .stream_callback = stream_http_callback,
        .callback_data = ctx,
        .low_speed_limit = 1,
        .low_speed_time = 30
    };

    // Execute streaming request
    int result = http_post_streaming(
        session->session_data.config.api_url,
        post_data,
        headers,
        &streaming_config
    );

    free(post_data);

    if (result != 0) {
        // Clean up any provider-specific streaming state (e.g., thread-local allocations)
        if (provider->cleanup_stream_state != NULL) {
            provider->cleanup_stream_state(provider);
        }
        streaming_context_free(ctx);
        fprintf(stderr, "Error: Streaming HTTP request failed\n");
        return -1;
    }

    // Save token counts to display after tool execution (if any)
    int input_tokens = ctx->input_tokens;
    int output_tokens = ctx->output_tokens;

    // Save user message to conversation
    if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
        fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
    }

    // Handle tool calls if any
    if (ctx->tool_uses.count > 0) {
        // Ensure count fits in int (required by downstream APIs)
        if (ctx->tool_uses.count > INT_MAX) {
            fprintf(stderr, "Error: Too many tool calls (%zu exceeds INT_MAX)\n", ctx->tool_uses.count);
            streaming_context_free(ctx);
            return -1;
        }
        int call_count = (int)ctx->tool_uses.count;

        // Convert streaming tool uses to ToolCall array
        ToolCall* tool_calls = malloc(ctx->tool_uses.count * sizeof(ToolCall));
        if (tool_calls != NULL) {
            for (size_t i = 0; i < ctx->tool_uses.count; i++) {
                tool_calls[i].id = ctx->tool_uses.data[i].id ? strdup(ctx->tool_uses.data[i].id) : NULL;
                tool_calls[i].name = ctx->tool_uses.data[i].name ? strdup(ctx->tool_uses.data[i].name) : NULL;
                tool_calls[i].arguments = ctx->tool_uses.data[i].arguments_json ? strdup(ctx->tool_uses.data[i].arguments_json) : NULL;
            }

            // For OpenAI, construct assistant message with tool_calls array
            // This is required for the conversation format to be valid
            char* constructed_message = construct_openai_assistant_message_with_tools(
                ctx->text_content, tool_calls, call_count);
            if (constructed_message != NULL) {
                if (append_conversation_message(&session->session_data.conversation, "assistant", constructed_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }
                free(constructed_message);
            }

            // Output in JSON mode: text content first, then tool calls
            if (session->session_data.config.json_output_mode) {
                // Output any text content that was streamed alongside tool calls
                if (ctx->text_content != NULL && ctx->text_len > 0) {
                    json_output_assistant_text(ctx->text_content, input_tokens, output_tokens);
                }
                json_output_assistant_tool_calls(ctx->tool_uses.data, call_count, input_tokens, output_tokens);
            }

            // Execute tool workflow
            result = ralph_execute_tool_workflow(session, tool_calls, call_count,
                                                 user_message, max_tokens, headers);

            cleanup_tool_calls(tool_calls, call_count);
        } else {
            result = -1;
        }
    } else {
        // No tool calls - save assistant response directly
        if (ctx->text_content != NULL && ctx->text_len > 0) {
            if (append_conversation_message(&session->session_data.conversation, "assistant", ctx->text_content) != 0) {
                fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
            }

            // Output text response in JSON mode
            if (session->session_data.config.json_output_mode) {
                json_output_assistant_text(ctx->text_content, input_tokens, output_tokens);
            }
        }
        // Display token counts for non-tool responses
        display_streaming_complete(input_tokens, output_tokens);
    }

    streaming_context_free(ctx);
    return result;
}
