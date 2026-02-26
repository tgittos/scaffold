#include "streaming_handler.h"
#include "context_enhancement.h"
#include "../network/api_common.h"
#include "../util/interrupt.h"
#include "tool_executor.h"
#include "conversation_state.h"
#include "../network/streaming.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../util/debug_output.h"
#include "../llm/llm_client.h"
#include "../ui/status_line.h"
#include "../plugin/hook_dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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
    status_line_set_idle();
    display_streaming_text(text, len);
}

static void streaming_thinking_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    display_streaming_thinking(text, len);
}

static void streaming_tool_start_callback(const char* id, const char* name, void* user_data) {
    (void)user_data;
    display_streaming_tool_start(id, name);
}

static void streaming_tool_delta_callback(const char* id, const char* json_delta, void* user_data) {
    (void)user_data;
    display_streaming_tool_delta(id, json_delta, json_delta ? strlen(json_delta) : 0);
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

int streaming_process_message(AgentSession* session, LLMProvider* provider,
                              const char* user_message, int max_tokens) {
    if (session == NULL || provider == NULL) {
        return -1;
    }

    EnhancedPromptParts parts;
    if (build_enhanced_prompt_parts(session, user_message, &parts) != 0) {
        return -1;
    }

    /* Plugin hook: pre_llm_send */
    hook_dispatch_pre_llm_send(&session->plugin_manager, session,
                                &parts.base_prompt, &parts.dynamic_context);

    SystemPromptParts sys_parts = {
        .base_prompt = parts.base_prompt,
        .dynamic_context = parts.dynamic_context
    };

    char* post_data = provider->build_streaming_request_json(
        provider,
        session->session_data.config.model,
        &sys_parts,
        &session->session_data.conversation,
        user_message,
        max_tokens,
        &session->tools
    );

    free_enhanced_prompt_parts(&parts);

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
    ctx->on_tool_use_delta = streaming_tool_delta_callback;
    ctx->on_stream_end = streaming_end_callback;
    ctx->on_error = streaming_error_callback;
    ctx->on_sse_data = streaming_sse_data_callback;

    status_line_set_busy("Requesting...");
    display_streaming_init();

    struct StreamingHTTPConfig streaming_config = {
        .base = DEFAULT_HTTP_CONFIG,
        .stream_callback = stream_http_callback,
        .callback_data = ctx,
        .low_speed_limit = 1,
        .low_speed_time = 30
    };

    const char* hdrs[8] = {0};
    if (provider->build_headers) {
        provider->build_headers(provider, session->session_data.config.api_key, hdrs, 8);
    }

    int result = llm_client_send_streaming(
        session->session_data.config.api_url,
        hdrs,
        post_data,
        &streaming_config
    );

    free(post_data);

    if (result != 0) {
        status_line_set_idle();
        if (provider->cleanup_stream_state != NULL) {
            provider->cleanup_stream_state(provider);
        }
        streaming_context_free(ctx);
        fprintf(stderr, "Error: Streaming HTTP request failed\n");
        return -1;
    }

    int input_tokens = ctx->input_tokens;
    int output_tokens = ctx->output_tokens;

    /* Plugin hook: post_llm_response */
    {
        char *hook_text = ctx->text_content ? strdup(ctx->text_content) : NULL;
        int hook_call_count = (int)ctx->tool_uses.count;
        ToolCall *hook_calls = NULL;
        if (hook_call_count > 0) {
            hook_calls = calloc(hook_call_count, sizeof(ToolCall));
            if (hook_calls) {
                for (int i = 0; i < hook_call_count; i++) {
                    hook_calls[i].name = ctx->tool_uses.data[i].name;
                    hook_calls[i].arguments = ctx->tool_uses.data[i].arguments_json;
                }
            } else {
                hook_call_count = 0;
            }
        }
        hook_dispatch_post_llm_response(&session->plugin_manager, session,
                                         &hook_text, hook_calls, hook_call_count);
        if (hook_text && ctx->text_content &&
            strcmp(hook_text, ctx->text_content) != 0) {
            char *new_text = strdup(hook_text);
            if (new_text) {
                free(ctx->text_content);
                ctx->text_content = new_text;
                ctx->text_len = strlen(new_text);
            }
        }
        free(hook_text);
        free(hook_calls);
    }

    if (user_message != NULL && strlen(user_message) > 0) {
        if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
            fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
        }
    }

    if (ctx->tool_uses.count > 0) {
        display_streaming_complete(input_tokens, output_tokens);

        /* Safe narrowing: size_t→int; checked against INT_MAX above */
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

            conversation_append_assistant(session, ctx->text_content, tool_calls, call_count);

            if (session->session_data.config.json_output_mode) {
                if (ctx->text_content != NULL && ctx->text_len > 0) {
                    json_output_assistant_text(ctx->text_content, input_tokens, output_tokens);
                }
                json_output_assistant_tool_calls_buffered(tool_calls, call_count, input_tokens, output_tokens);
            }

            result = session_execute_tool_workflow(session, tool_calls, call_count,
                                                 user_message, max_tokens);

            cleanup_tool_calls(tool_calls, call_count);
        } else {
            result = -1;
        }
    } else {
        conversation_append_assistant(session, ctx->text_content, NULL, 0);

        if (ctx->text_content != NULL && ctx->text_len > 0 && session->session_data.config.json_output_mode) {
            json_output_assistant_text(ctx->text_content, input_tokens, output_tokens);
        }
        display_streaming_complete(input_tokens, output_tokens);
    }

    streaming_context_free(ctx);
    return result;
}
