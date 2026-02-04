#include "streaming_handler.h"
#include "context_enhancement.h"
#include "../util/interrupt.h"
#include "tool_executor.h"
#include "../network/streaming.h"
#include "../llm/llm_provider.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../util/debug_output.h"
#include "../network/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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

typedef struct {
    StreamingContext* ctx;
    LLMProvider* provider;
} StreamingSSEUserData;

static void streaming_sse_data_callback(const char* data, size_t len, void* user_data) {
    if (data == NULL || len == 0 || user_data == NULL) {
        return;
    }

    if (interrupt_pending()) {
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

static void streaming_text_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    display_streaming_text(text, len);
}

static void streaming_thinking_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    display_streaming_thinking(text, len);
}

static void streaming_tool_start_callback(const char* id, const char* name, void* user_data) {
    (void)id;
    (void)user_data;
    display_streaming_tool_start(name);
}

static void streaming_end_callback(const char* stop_reason, void* user_data) {
    (void)stop_reason;
    (void)user_data;
    // Completion display deferred until after HTTP returns, when token counts are available
}

static void streaming_error_callback(const char* error, void* user_data) {
    (void)user_data;
    display_streaming_error(error);
}

static size_t stream_http_callback(const char* data, size_t size, void* user_data) {
    if (data == NULL || size == 0 || user_data == NULL) {
        return 0;
    }

    StreamingContext* ctx = (StreamingContext*)user_data;

    if (streaming_process_chunk(ctx, data, size) != 0) {
        return 0;
    }

    return size;
}

int streaming_process_message(AgentSession* session, const char* user_message,
                              int max_tokens, const char** headers) {
    if (session == NULL || user_message == NULL) {
        return -1;
    }

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

    char* final_prompt = build_enhanced_prompt_with_context(session, user_message);
    if (final_prompt == NULL) {
        return -1;
    }

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

    StreamingContext* ctx = streaming_context_create();
    if (ctx == NULL) {
        free(post_data);
        fprintf(stderr, "Error: Failed to create streaming context\n");
        return -1;
    }

    StreamingSSEUserData sse_user_data = {
        .ctx = ctx,
        .provider = provider
    };
    ctx->user_data = &sse_user_data;

    ctx->on_text_chunk = streaming_text_callback;
    ctx->on_thinking_chunk = streaming_thinking_callback;
    ctx->on_tool_use_start = streaming_tool_start_callback;
    ctx->on_stream_end = streaming_end_callback;
    ctx->on_error = streaming_error_callback;
    ctx->on_sse_data = streaming_sse_data_callback;

    display_streaming_init();

    struct StreamingHTTPConfig streaming_config = {
        .base = DEFAULT_HTTP_CONFIG,
        .stream_callback = stream_http_callback,
        .callback_data = ctx,
        .low_speed_limit = 1,
        .low_speed_time = 30
    };

    int result = http_post_streaming(
        session->session_data.config.api_url,
        post_data,
        headers,
        &streaming_config
    );

    free(post_data);

    if (result != 0) {
        if (provider->cleanup_stream_state != NULL) {
            provider->cleanup_stream_state(provider);
        }
        streaming_context_free(ctx);
        fprintf(stderr, "Error: Streaming HTTP request failed\n");
        return -1;
    }

    int input_tokens = ctx->input_tokens;
    int output_tokens = ctx->output_tokens;

    if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
        fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
    }

    if (ctx->tool_uses.count > 0) {
        // Downstream APIs use int for call_count
        if (ctx->tool_uses.count > INT_MAX) {
            fprintf(stderr, "Error: Too many tool calls (%zu exceeds INT_MAX)\n", ctx->tool_uses.count);
            streaming_context_free(ctx);
            return -1;
        }
        int call_count = (int)ctx->tool_uses.count;

        ToolCall* tool_calls = malloc(ctx->tool_uses.count * sizeof(ToolCall));
        if (tool_calls != NULL) {
            for (size_t i = 0; i < ctx->tool_uses.count; i++) {
                tool_calls[i].id = ctx->tool_uses.data[i].id ? strdup(ctx->tool_uses.data[i].id) : NULL;
                tool_calls[i].name = ctx->tool_uses.data[i].name ? strdup(ctx->tool_uses.data[i].name) : NULL;
                tool_calls[i].arguments = ctx->tool_uses.data[i].arguments_json ? strdup(ctx->tool_uses.data[i].arguments_json) : NULL;
            }

            // OpenAI conversation format requires assistant messages to include the tool_calls array
            char* constructed_message = construct_openai_assistant_message_with_tools(
                ctx->text_content, tool_calls, call_count);
            if (constructed_message != NULL) {
                if (append_conversation_message(&session->session_data.conversation, "assistant", constructed_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }
                free(constructed_message);
            }

            if (session->session_data.config.json_output_mode) {
                if (ctx->text_content != NULL && ctx->text_len > 0) {
                    json_output_assistant_text(ctx->text_content, input_tokens, output_tokens);
                }
                json_output_assistant_tool_calls(ctx->tool_uses.data, call_count, input_tokens, output_tokens);
            }

            result = session_execute_tool_workflow(session, tool_calls, call_count,
                                                 user_message, max_tokens, headers);

            cleanup_tool_calls(tool_calls, call_count);
        } else {
            result = -1;
        }
    } else {
        if (ctx->text_content != NULL && ctx->text_len > 0) {
            if (append_conversation_message(&session->session_data.conversation, "assistant", ctx->text_content) != 0) {
                fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
            }

            if (session->session_data.config.json_output_mode) {
                json_output_assistant_text(ctx->text_content, input_tokens, output_tokens);
            }
        }
        display_streaming_complete(input_tokens, output_tokens);
    }

    streaming_context_free(ctx);
    return result;
}
