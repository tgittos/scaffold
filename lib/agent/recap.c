#include "recap.h"
#include "../network/streaming.h"
#include "../llm/llm_provider.h"
#include "../llm/llm_client.h"
#include "../ui/output_formatter.h"
#include "../ui/status_line.h"
#include "../util/debug_output.h"
#include "../session/token_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECAP_DEFAULT_MAX_MESSAGES 5
#define RECAP_INITIAL_BUFFER_SIZE 4096

static char* format_recent_messages_for_recap(const ConversationHistory* history, int max_messages) {
    if (history == NULL || history->count == 0) {
        return NULL;
    }

    int start_index = 0;
    int message_count = history->count;
    if (max_messages > 0 && message_count > max_messages) {
        start_index = message_count - max_messages;
        message_count = max_messages;
    }

    size_t buffer_size = RECAP_INITIAL_BUFFER_SIZE;
    for (size_t i = (size_t)start_index; i < history->count; i++) {
        if (history->data[i].content != NULL) {
            buffer_size += strlen(history->data[i].content) + 64;
        }
    }

    char* buffer = malloc(buffer_size);
    if (buffer == NULL) {
        return NULL;
    }
    buffer[0] = '\0';

    size_t offset = 0;
    for (size_t i = (size_t)start_index; i < history->count; i++) {
        const ConversationMessage* msg = &history->data[i];

        // Tool messages are implementation detail noise in a recap
        if (msg->role != NULL && strcmp(msg->role, "tool") == 0) {
            continue;
        }

        const char* role = msg->role ? msg->role : "unknown";
        const char* content = msg->content ? msg->content : "";

        const int max_content_length = 500;
        char truncated_content[512];
        if (strlen(content) > (size_t)max_content_length) {
            strncpy(truncated_content, content, max_content_length - 3);
            truncated_content[max_content_length - 3] = '\0';
            strcat(truncated_content, "...");
            content = truncated_content;
        }

        int written = snprintf(buffer + offset, buffer_size - offset,
                              "**%s**: %s\n\n", role, content);
        if (written < 0 || (size_t)written >= buffer_size - offset) {
            break; // Buffer full
        }
        offset += written;
    }

    return buffer;
}

typedef struct {
    StreamingContext* ctx;
    LLMProvider* provider;
} RecapSSEUserData;

static void recap_sse_data_callback(const char* data, size_t len, void* user_data) {
    if (data == NULL || len == 0 || user_data == NULL) {
        return;
    }

    RecapSSEUserData* sse_data = (RecapSSEUserData*)user_data;
    if (sse_data->ctx == NULL || sse_data->provider == NULL) {
        return;
    }

    if (sse_data->provider->parse_stream_event != NULL) {
        sse_data->provider->parse_stream_event(sse_data->provider, sse_data->ctx, data, len);
    }
}

static void recap_text_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    status_line_set_idle();
    display_streaming_text(text, len);
}

static void recap_thinking_callback(const char* text, size_t len, void* user_data) {
    (void)user_data;
    display_streaming_thinking(text, len);
}

static void recap_end_callback(const char* stop_reason, void* user_data) {
    (void)stop_reason;
    (void)user_data;
}

static void recap_error_callback(const char* error, void* user_data) {
    (void)user_data;
    display_streaming_error(error);
}

static size_t recap_stream_http_callback(const char* data, size_t size, void* user_data) {
    if (data == NULL || size == 0 || user_data == NULL) {
        return 0;
    }

    StreamingContext* ctx = (StreamingContext*)user_data;

    if (streaming_process_chunk(ctx, data, size) != 0) {
        return 0;
    }

    return size;
}

int recap_generate(AgentSession* session, int max_messages) {
    if (session == NULL) {
        return -1;
    }

    const ConversationHistory* history = &session->session_data.conversation;
    if (history->count == 0) {
        return 0;
    }
    if (max_messages <= 0) {
        max_messages = RECAP_DEFAULT_MAX_MESSAGES;
    }

    char* recent_messages = format_recent_messages_for_recap(history, max_messages);
    if (recent_messages == NULL) {
        return -1;
    }

    const char* recap_template =
        "You are resuming a conversation. Here are the most recent messages:\n\n"
        "%s\n"
        "Please provide a very brief recap (2-3 sentences max) of what was being discussed, "
        "and ask how you can continue to help. Be warm and conversational.";

    size_t prompt_size = strlen(recap_template) + strlen(recent_messages) + 1;
    char* recap_prompt = malloc(prompt_size);
    if (recap_prompt == NULL) {
        free(recent_messages);
        return -1;
    }

    snprintf(recap_prompt, prompt_size, recap_template, recent_messages);
    free(recent_messages);

    debug_printf("Generating recap with prompt: %s\n", recap_prompt);

    ProviderRegistry* registry = get_provider_registry();
    if (registry == NULL) {
        fprintf(stderr, "Error: Failed to get provider registry for recap\n");
        free(recap_prompt);
        return -1;
    }

    LLMProvider* provider = detect_provider_for_url(registry, session->session_data.config.api_url);
    if (provider == NULL) {
        fprintf(stderr, "Error: No provider found for URL: %s\n", session->session_data.config.api_url);
        free(recap_prompt);
        return -1;
    }

    // Empty history: recap is a one-shot call, not part of the conversation
    ConversationHistory empty_history = {0};
    int max_tokens = 300;

    char* post_data = provider->build_streaming_request_json(
        provider,
        session->session_data.config.model,
        session->session_data.config.system_prompt,
        &empty_history,
        recap_prompt,
        max_tokens,
        NULL
    );

    free(recap_prompt);

    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build recap streaming JSON payload\n");
        return -1;
    }

    debug_printf("Making recap streaming API request to %s\n", session->session_data.config.api_url);
    debug_printf("POST data: %s\n\n", post_data);

    StreamingContext* ctx = streaming_context_create();
    if (ctx == NULL) {
        free(post_data);
        fprintf(stderr, "Error: Failed to create streaming context for recap\n");
        return -1;
    }

    RecapSSEUserData sse_user_data = {
        .ctx = ctx,
        .provider = provider
    };
    ctx->user_data = &sse_user_data;

    ctx->on_text_chunk = recap_text_callback;
    ctx->on_thinking_chunk = recap_thinking_callback;
    ctx->on_stream_end = recap_end_callback;
    ctx->on_error = recap_error_callback;
    ctx->on_sse_data = recap_sse_data_callback;

    status_line_set_busy("Requesting...");
    display_streaming_init();

    struct StreamingHTTPConfig streaming_config = {
        .base = DEFAULT_HTTP_CONFIG,
        .stream_callback = recap_stream_http_callback,
        .callback_data = ctx,
        .low_speed_limit = 1,
        .low_speed_time = 30
    };

    int result = llm_client_send_streaming(
        session->session_data.config.api_url,
        session->session_data.config.api_key,
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
        fprintf(stderr, "Error: Recap streaming HTTP request failed\n");
        return -1;
    }

    display_streaming_complete(ctx->input_tokens, ctx->output_tokens);
    streaming_context_free(ctx);

    // Recap is intentionally not saved to conversation history to avoid bloat

    return 0;
}
