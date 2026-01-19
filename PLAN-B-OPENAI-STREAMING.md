# Plan B: OpenAI Streaming Integration

## Prerequisites

- Plan A complete (streaming foundation)

## Goal

Integrate streaming with the OpenAI provider and REPL display. After this plan, users see OpenAI responses stream in real-time.

## OpenAI SSE Format

OpenAI streams JSON objects prefixed with `data: `:

```
data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"role":"assistant"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":"Hello"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{"content":" world"},"finish_reason":null}]}

data: {"id":"chatcmpl-123","object":"chat.completion.chunk","choices":[{"index":0,"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":10,"completion_tokens":5}}

data: [DONE]
```

**Tool calls stream as:**
```
data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call_abc","type":"function","function":{"name":"get_weather","arguments":""}}]}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"{\"loc"}}]}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"ation\":"}}]}}]}

data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"\"NYC\"}"}}]}}]}
```

## Deliverables

### 1. OpenAI Event Parser

**Modify:** `src/llm/providers/openai_provider.c`

```c
// Parse a single SSE data line from OpenAI
int openai_parse_stream_event(StreamingContext* ctx, const char* json_data, size_t len);
```

Parsing logic:
1. Skip if `[DONE]`
2. Parse JSON
3. Extract `choices[0].delta`
4. If `delta.content` exists → `streaming_emit_text()`
5. If `delta.tool_calls` exists → handle tool streaming
6. If `finish_reason` is set → `streaming_emit_complete()`
7. If `usage` exists → extract token counts

```c
int openai_parse_stream_event(StreamingContext* ctx, const char* json_data, size_t len) {
    if (len == 6 && memcmp(json_data, "[DONE]", 6) == 0) {
        return 0; // Stream complete signal
    }

    cJSON* root = cJSON_ParseWithLength(json_data, len);
    if (!root) return -1;

    cJSON* choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_GetArraySize(choices) > 0) {
        cJSON* choice = cJSON_GetArrayItem(choices, 0);
        cJSON* delta = cJSON_GetObjectItem(choice, "delta");

        if (delta) {
            // Text content
            cJSON* content = cJSON_GetObjectItem(delta, "content");
            if (content && cJSON_IsString(content)) {
                streaming_emit_text(ctx, content->valuestring, strlen(content->valuestring));
            }

            // Tool calls
            cJSON* tool_calls = cJSON_GetObjectItem(delta, "tool_calls");
            if (tool_calls) {
                openai_parse_tool_call_delta(ctx, tool_calls);
            }
        }

        // Finish reason
        cJSON* finish = cJSON_GetObjectItem(choice, "finish_reason");
        if (finish && cJSON_IsString(finish)) {
            streaming_emit_complete(ctx, finish->valuestring);
        }
    }

    // Usage (appears in final message with stream_options.include_usage)
    cJSON* usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        ctx->input_tokens = cJSON_GetObjectItem(usage, "prompt_tokens")->valueint;
        ctx->output_tokens = cJSON_GetObjectItem(usage, "completion_tokens")->valueint;
    }

    cJSON_Delete(root);
    return 0;
}
```

### 2. Provider Interface Extension

**Modify:** `src/llm/llm_provider.h`

```c
struct LLMProvider {
    // ... existing fields ...

    // Streaming support
    int (*supports_streaming)(const struct LLMProvider* provider);
    int (*parse_stream_event)(StreamingContext* ctx, const char* data, size_t len);
    char* (*build_request_json)(const struct LLMProvider* provider,
                                 const Message* messages, int message_count,
                                 const Tool* tools, int tool_count,
                                 bool streaming);  // Add streaming parameter
};
```

**Modify:** `src/llm/providers/openai_provider.c`

```c
static int openai_supports_streaming(const struct LLMProvider* provider) {
    return 1; // OpenAI supports streaming
}

static char* openai_build_request_json(..., bool streaming) {
    // ... existing code ...

    if (streaming) {
        cJSON_AddBoolToObject(root, "stream", true);

        // Request usage stats in stream
        cJSON* stream_options = cJSON_CreateObject();
        cJSON_AddBoolToObject(stream_options, "include_usage", true);
        cJSON_AddItemToObject(root, "stream_options", stream_options);
    }

    // ... rest of existing code ...
}
```

### 3. REPL Streaming Display

**Modify:** `src/utils/output_formatter.h`

```c
// Streaming display functions
void display_streaming_init(void);
void display_streaming_text(const char* text, size_t len);
void display_streaming_thinking(const char* text, size_t len);
void display_streaming_tool_start(const char* tool_name);
void display_streaming_complete(int input_tokens, int output_tokens);
void display_streaming_error(const char* error);
```

**Modify:** `src/utils/output_formatter.c`

```c
void display_streaming_init(void) {
    // Clear any "thinking" indicator
    // Position cursor for streaming output
    fflush(stdout);
}

void display_streaming_text(const char* text, size_t len) {
    fwrite(text, 1, len, stdout);
    fflush(stdout);
}

void display_streaming_thinking(const char* text, size_t len) {
    // Display in dimmed/gray style if terminal supports it
    printf("\033[90m");  // Gray
    fwrite(text, 1, len, stdout);
    printf("\033[0m");   // Reset
    fflush(stdout);
}

void display_streaming_tool_start(const char* tool_name) {
    printf("\n[Calling %s...]\n", tool_name);
    fflush(stdout);
}

void display_streaming_complete(int input_tokens, int output_tokens) {
    // Final newline and optional token display
    printf("\n");
    fflush(stdout);
}
```

### 4. Core Integration

**Modify:** `src/core/ralph.c`

Add streaming message processing:

```c
// Streaming callback that connects to display
static size_t ralph_stream_callback(const char* data, size_t len, void* user_data) {
    StreamingContext* ctx = (StreamingContext*)user_data;
    return streaming_process_chunk(ctx, data, len);
}

int ralph_process_message_streaming(RalphSession* session, const char* user_message) {
    LLMProvider* provider = session->provider;

    // Build streaming request
    char* request_json = provider->build_request_json(
        provider, session->messages, session->message_count,
        session->tools, session->tool_count, true /* streaming */);

    // Setup streaming context with display callbacks
    StreamingContext* ctx = streaming_context_create();
    ctx->on_text_chunk = display_streaming_text_callback;
    ctx->on_tool_use_start = display_streaming_tool_callback;
    ctx->on_stream_end = display_streaming_end_callback;
    ctx->user_data = session;

    // Wire up provider-specific parser
    // The SSE parser calls provider->parse_stream_event for each data line

    display_streaming_init();

    // Execute streaming request
    struct StreamingHTTPConfig config = {
        .base = session->http_config,
        .stream_callback = ralph_stream_callback,
        .callback_data = ctx,
        .low_speed_limit = 1,
        .low_speed_time = 30
    };

    int result = http_post_streaming(provider->endpoint, request_json,
                                      provider->headers, &config);

    // Process results (handle tool calls, update conversation)
    if (ctx->tool_use_count > 0) {
        // Execute tools and continue conversation
        ralph_execute_tool_calls(session, ctx->tool_uses, ctx->tool_use_count);
    }

    // Update conversation history with assistant response
    ralph_add_assistant_message(session, ctx->text_content,
                                 ctx->tool_uses, ctx->tool_use_count);

    streaming_context_free(ctx);
    free(request_json);

    return result;
}
```

**Modify:** `ralph_process_message()` to use streaming:

```c
int ralph_process_message(RalphSession* session, const char* user_message) {
    LLMProvider* provider = session->provider;

    // Use streaming if provider supports it
    if (provider->supports_streaming && provider->supports_streaming(provider)) {
        return ralph_process_message_streaming(session, user_message);
    }

    // Fall back to existing buffered implementation
    // ... existing code ...
}
```

## Files Summary

| File | Action |
|------|--------|
| `src/llm/llm_provider.h` | MODIFY - add streaming methods |
| `src/llm/providers/openai_provider.c` | MODIFY - SSE parser, streaming request |
| `src/utils/output_formatter.h` | MODIFY - streaming display declarations |
| `src/utils/output_formatter.c` | MODIFY - streaming display implementation |
| `src/core/ralph.c` | MODIFY - `ralph_process_message_streaming()` |

## Testing

**New file:** `test/llm/test_openai_streaming.c`

Unit tests:
1. Parse text content delta
2. Parse tool call start
3. Parse tool call argument deltas
4. Parse finish_reason
5. Parse usage stats
6. Handle `[DONE]` signal
7. Handle malformed JSON gracefully

**Integration test:** `test/integration/test_streaming_e2e.c`

1. Full streaming conversation with text response
2. Streaming response with tool call
3. Multi-turn streaming conversation

**Test fixtures:**
- `test/fixtures/openai_stream_text.txt` - Sample text streaming sequence
- `test/fixtures/openai_stream_tool.txt` - Sample tool call streaming sequence

## Verification

1. `make clean && make` - builds without errors
2. `make test` - all tests pass including new streaming tests
3. Manual test with real OpenAI API:
   - Simple prompt streams text token-by-token
   - Tool-using prompt shows tool name during stream
4. `make check-valgrind` - no memory leaks

## Out of Scope

- Configuration options for streaming (Plan C)
- Fallback/disable options (Plan C)
- Anthropic provider (Plan D)
