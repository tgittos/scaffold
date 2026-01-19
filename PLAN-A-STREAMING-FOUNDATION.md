# Plan A: Core Streaming Foundation

## Goal

Build the foundational streaming infrastructure: SSE parser, streaming HTTP support, and core data structures. This plan delivers testable components without provider integration.

## Current State

- HTTP client (`src/network/http_client.c`) buffers complete responses before returning
- No SSE (Server-Sent Events) parsing capability
- No streaming callback infrastructure

## Deliverables

### 1. Streaming Data Structures

**New file:** `src/network/streaming.h`

```c
typedef enum {
    STREAM_STATE_IDLE,
    STREAM_STATE_READING_EVENT,
    STREAM_STATE_READING_DATA,
    STREAM_STATE_COMPLETE,
    STREAM_STATE_ERROR
} StreamState;

typedef struct {
    char* id;
    char* name;
    char* arguments_json;      // Accumulated from partial deltas
    size_t arguments_capacity;
} StreamingToolUse;

typedef struct {
    StreamState state;

    // SSE line buffering
    char* line_buffer;
    size_t line_buffer_len;
    size_t line_buffer_capacity;

    // Accumulated content
    char* text_content;
    size_t text_len;
    size_t text_capacity;

    char* thinking_content;
    size_t thinking_len;
    size_t thinking_capacity;

    // Tool calls
    StreamingToolUse* tool_uses;
    int tool_use_count;
    int tool_use_capacity;

    // Metadata
    int input_tokens;
    int output_tokens;
    char* stop_reason;
    char* error_message;

    // Callbacks (optional, for real-time display)
    void (*on_text_chunk)(const char* text, size_t len, void* user_data);
    void (*on_thinking_chunk)(const char* text, size_t len, void* user_data);
    void (*on_tool_use_start)(const char* id, const char* name, void* user_data);
    void (*on_tool_use_delta)(const char* id, const char* json_delta, void* user_data);
    void (*on_stream_end)(const char* stop_reason, void* user_data);
    void (*on_error)(const char* error, void* user_data);
    void* user_data;
} StreamingContext;

// Lifecycle
StreamingContext* streaming_context_create(void);
void streaming_context_free(StreamingContext* ctx);
void streaming_context_reset(StreamingContext* ctx);

// SSE parsing
int streaming_process_chunk(StreamingContext* ctx, const char* data, size_t len);
int streaming_process_sse_line(StreamingContext* ctx, const char* line, size_t len);

// Event dispatch (called by provider-specific parsers)
void streaming_emit_text(StreamingContext* ctx, const char* text, size_t len);
void streaming_emit_thinking(StreamingContext* ctx, const char* text, size_t len);
void streaming_emit_tool_start(StreamingContext* ctx, const char* id, const char* name);
void streaming_emit_tool_delta(StreamingContext* ctx, const char* id, const char* json_delta, size_t len);
void streaming_emit_complete(StreamingContext* ctx, const char* stop_reason);
void streaming_emit_error(StreamingContext* ctx, const char* error);
```

### 2. SSE Parser Implementation

**New file:** `src/network/streaming.c`

Core SSE parsing logic:
- Buffer incomplete lines across chunk boundaries
- Parse `data: {...}\n\n` format
- Handle `event:` and `id:` fields (for future use)
- Handle `data: [DONE]` termination signal

```c
// SSE line states
// - Accumulate bytes until \n
// - Empty line (\n\n) signals end of event
// - "data: " prefix contains JSON payload
// - "event: " prefix contains event type (optional)

int streaming_process_chunk(StreamingContext* ctx, const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\n') {
            // Process accumulated line
            if (ctx->line_buffer_len > 0) {
                streaming_process_sse_line(ctx, ctx->line_buffer, ctx->line_buffer_len);
                ctx->line_buffer_len = 0;
            }
            // Empty line after data = event complete (handled by provider parser)
        } else {
            // Accumulate into line buffer (with realloc if needed)
            streaming_append_char(ctx, c);
        }
    }
    return 0;
}
```

### 3. Streaming HTTP Support

**Modify:** `src/network/http_client.h`

```c
typedef size_t (*http_stream_callback_t)(const char* data, size_t size, void* user_data);

struct StreamingHTTPConfig {
    struct HTTPConfig base;
    http_stream_callback_t stream_callback;
    void* callback_data;
    long low_speed_limit;    // Bytes/sec threshold for timeout (default: 1)
    long low_speed_time;     // Seconds below threshold before timeout (default: 30)
};

int http_post_streaming(const char* url, const char* post_data,
                        const char** headers,
                        const struct StreamingHTTPConfig* config);
```

**Modify:** `src/network/http_client.c`

```c
// Internal callback wrapper for libcurl
static size_t streaming_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    struct StreamingHTTPConfig* config = (struct StreamingHTTPConfig*)userp;
    size_t total = size * nmemb;

    if (config->stream_callback) {
        return config->stream_callback((const char*)contents, total, config->callback_data);
    }
    return total;
}

int http_post_streaming(const char* url, const char* post_data,
                        const char** headers,
                        const struct StreamingHTTPConfig* config) {
    CURL* curl = curl_easy_init();
    // ... standard setup ...

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streaming_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)config);

    // Streaming-specific timeouts
    if (config->low_speed_limit > 0) {
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, config->low_speed_limit);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, config->low_speed_time);
    }

    // ... execute and cleanup ...
}
```

## Files Summary

| File | Action |
|------|--------|
| `src/network/streaming.h` | NEW |
| `src/network/streaming.c` | NEW |
| `src/network/http_client.h` | MODIFY - add streaming types/function |
| `src/network/http_client.c` | MODIFY - implement `http_post_streaming` |
| `Makefile` | MODIFY - add streaming.o |

## Testing

**New file:** `test/network/test_streaming.c`

Unit tests for:
1. SSE line parsing - complete lines, partial lines, multi-line chunks
2. Line buffer growth - handle very long lines
3. `data: [DONE]` detection
4. Empty line handling (event boundaries)
5. Context lifecycle (create, reset, free)
6. Text/thinking accumulation via emit functions
7. Tool use accumulation via emit functions

```c
void test_sse_complete_line(void) {
    StreamingContext* ctx = streaming_context_create();
    streaming_process_chunk(ctx, "data: {\"type\":\"test\"}\n\n", 24);
    // Verify line was processed
    streaming_context_free(ctx);
}

void test_sse_partial_lines(void) {
    StreamingContext* ctx = streaming_context_create();
    streaming_process_chunk(ctx, "data: {\"ty", 10);
    streaming_process_chunk(ctx, "pe\":\"test\"}\n\n", 14);
    // Verify line was correctly reassembled
    streaming_context_free(ctx);
}

void test_emit_text_accumulates(void) {
    StreamingContext* ctx = streaming_context_create();
    streaming_emit_text(ctx, "Hello ", 6);
    streaming_emit_text(ctx, "World", 5);
    TEST_ASSERT_EQUAL_STRING("Hello World", ctx->text_content);
    streaming_context_free(ctx);
}
```

## Verification

1. `make clean && make` - builds without errors
2. `make test` - new streaming tests pass
3. `make check-valgrind` - no memory leaks in streaming code

## Out of Scope

- Provider-specific event parsing (Plan B)
- REPL display integration (Plan B)
- Configuration options (Plan C)
- Anthropic support (Plan D)
