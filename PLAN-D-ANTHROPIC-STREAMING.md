# Plan D: Anthropic Streaming Support

## Prerequisites

- Plan A complete (streaming foundation)
- Plan B complete (OpenAI streaming)
- Plan C complete (configuration)

## Goal

Add streaming support for the Anthropic provider, enabling real-time output for Claude models.

## Anthropic SSE Format

Anthropic uses a more structured event system:

```
event: message_start
data: {"type":"message_start","message":{"id":"msg_123","type":"message","role":"assistant","content":[],"model":"claude-3-opus-20240229","usage":{"input_tokens":25}}}

event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":" world"}}

event: content_block_stop
data: {"type":"content_block_stop","index":0}

event: message_delta
data: {"type":"message_delta","delta":{"stop_reason":"end_turn"},"usage":{"output_tokens":12}}

event: message_stop
data: {"type":"message_stop"}
```

**Thinking blocks:**
```
event: content_block_start
data: {"type":"content_block_start","index":0,"content_block":{"type":"thinking","thinking":""}}

event: content_block_delta
data: {"type":"content_block_delta","index":0,"delta":{"type":"thinking_delta","thinking":"Let me think..."}}
```

**Tool use blocks:**
```
event: content_block_start
data: {"type":"content_block_start","index":1,"content_block":{"type":"tool_use","id":"toolu_123","name":"get_weather"}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"{\"loc"}}

event: content_block_delta
data: {"type":"content_block_delta","index":1,"delta":{"type":"input_json_delta","partial_json":"ation\":"}}
```

## Deliverables

### 1. Anthropic Event Parser

**Modify:** `src/llm/providers/anthropic_provider.c`

```c
typedef enum {
    ANTHROPIC_EVENT_UNKNOWN,
    ANTHROPIC_EVENT_MESSAGE_START,
    ANTHROPIC_EVENT_CONTENT_BLOCK_START,
    ANTHROPIC_EVENT_CONTENT_BLOCK_DELTA,
    ANTHROPIC_EVENT_CONTENT_BLOCK_STOP,
    ANTHROPIC_EVENT_MESSAGE_DELTA,
    ANTHROPIC_EVENT_MESSAGE_STOP,
    ANTHROPIC_EVENT_PING,
    ANTHROPIC_EVENT_ERROR
} AnthropicEventType;

// Track current content block type for delta routing
typedef struct {
    int current_block_index;
    char* current_block_type;  // "text", "thinking", "tool_use"
    char* current_tool_id;
} AnthropicStreamState;

int anthropic_parse_stream_event(StreamingContext* ctx,
                                  const char* event_type,
                                  const char* json_data,
                                  size_t len);
```

Parsing logic:

```c
int anthropic_parse_stream_event(StreamingContext* ctx,
                                  const char* event_type,
                                  const char* json_data,
                                  size_t len) {
    AnthropicStreamState* state = (AnthropicStreamState*)ctx->provider_state;

    cJSON* root = cJSON_ParseWithLength(json_data, len);
    if (!root) return -1;

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (!type) {
        cJSON_Delete(root);
        return -1;
    }

    const char* type_str = type->valuestring;

    if (strcmp(type_str, "message_start") == 0) {
        cJSON* message = cJSON_GetObjectItem(root, "message");
        cJSON* usage = cJSON_GetObjectItem(message, "usage");
        if (usage) {
            ctx->input_tokens = cJSON_GetObjectItem(usage, "input_tokens")->valueint;
        }
    }
    else if (strcmp(type_str, "content_block_start") == 0) {
        cJSON* block = cJSON_GetObjectItem(root, "content_block");
        cJSON* block_type = cJSON_GetObjectItem(block, "type");

        state->current_block_index = cJSON_GetObjectItem(root, "index")->valueint;
        free(state->current_block_type);
        state->current_block_type = strdup(block_type->valuestring);

        if (strcmp(block_type->valuestring, "tool_use") == 0) {
            cJSON* id = cJSON_GetObjectItem(block, "id");
            cJSON* name = cJSON_GetObjectItem(block, "name");
            free(state->current_tool_id);
            state->current_tool_id = strdup(id->valuestring);
            streaming_emit_tool_start(ctx, id->valuestring, name->valuestring);
        }
    }
    else if (strcmp(type_str, "content_block_delta") == 0) {
        cJSON* delta = cJSON_GetObjectItem(root, "delta");
        cJSON* delta_type = cJSON_GetObjectItem(delta, "type");

        if (strcmp(delta_type->valuestring, "text_delta") == 0) {
            cJSON* text = cJSON_GetObjectItem(delta, "text");
            streaming_emit_text(ctx, text->valuestring, strlen(text->valuestring));
        }
        else if (strcmp(delta_type->valuestring, "thinking_delta") == 0) {
            cJSON* thinking = cJSON_GetObjectItem(delta, "thinking");
            streaming_emit_thinking(ctx, thinking->valuestring, strlen(thinking->valuestring));
        }
        else if (strcmp(delta_type->valuestring, "input_json_delta") == 0) {
            cJSON* partial = cJSON_GetObjectItem(delta, "partial_json");
            streaming_emit_tool_delta(ctx, state->current_tool_id,
                                       partial->valuestring, strlen(partial->valuestring));
        }
    }
    else if (strcmp(type_str, "message_delta") == 0) {
        cJSON* delta = cJSON_GetObjectItem(root, "delta");
        cJSON* stop_reason = cJSON_GetObjectItem(delta, "stop_reason");
        if (stop_reason) {
            streaming_emit_complete(ctx, stop_reason->valuestring);
        }

        cJSON* usage = cJSON_GetObjectItem(root, "usage");
        if (usage) {
            ctx->output_tokens = cJSON_GetObjectItem(usage, "output_tokens")->valueint;
        }
    }
    else if (strcmp(type_str, "error") == 0) {
        cJSON* error = cJSON_GetObjectItem(root, "error");
        cJSON* message = cJSON_GetObjectItem(error, "message");
        streaming_emit_error(ctx, message->valuestring);
    }

    cJSON_Delete(root);
    return 0;
}
```

### 2. SSE Event Type Parsing

Anthropic uses `event:` lines before `data:` lines. Extend the SSE parser:

**Modify:** `src/network/streaming.c`

```c
typedef struct {
    char* current_event_type;  // From "event: xxx" line
} SSEParserState;

int streaming_process_sse_line(StreamingContext* ctx, const char* line, size_t len) {
    SSEParserState* sse = &ctx->sse_state;

    if (len > 6 && strncmp(line, "event:", 6) == 0) {
        // Store event type for next data line
        const char* event = line + 6;
        while (*event == ' ') event++;
        free(sse->current_event_type);
        sse->current_event_type = strndup(event, len - (event - line));
    }
    else if (len > 5 && strncmp(line, "data:", 5) == 0) {
        const char* data = line + 5;
        while (*data == ' ') data++;
        size_t data_len = len - (data - line);

        // Dispatch to provider parser with event type
        if (ctx->provider_parse_event) {
            ctx->provider_parse_event(ctx, sse->current_event_type, data, data_len);
        }

        // Clear event type after use
        free(sse->current_event_type);
        sse->current_event_type = NULL;
    }

    return 0;
}
```

### 3. Provider Interface Update

**Modify:** `src/llm/providers/anthropic_provider.c`

```c
static int anthropic_supports_streaming(const struct LLMProvider* provider) {
    return 1;
}

static char* anthropic_build_request_json(..., bool streaming) {
    // ... existing code ...

    if (streaming) {
        cJSON_AddBoolToObject(root, "stream", true);
    }

    // ... rest of existing code ...
}

// Register in provider init
provider->supports_streaming = anthropic_supports_streaming;
provider->parse_stream_event = anthropic_parse_stream_event;
provider->build_request_json = anthropic_build_request_json;
```

### 4. Provider State Management

**Modify:** `src/network/streaming.h`

```c
typedef struct {
    // ... existing fields ...

    // Provider-specific state (opaque)
    void* provider_state;
    void (*provider_state_free)(void* state);

    // Provider event parser
    int (*provider_parse_event)(StreamingContext* ctx,
                                 const char* event_type,
                                 const char* data,
                                 size_t len);
} StreamingContext;
```

## Files Summary

| File | Action |
|------|--------|
| `src/llm/providers/anthropic_provider.c` | MODIFY - event parser, streaming support |
| `src/network/streaming.h` | MODIFY - event type support, provider state |
| `src/network/streaming.c` | MODIFY - event: line parsing |

## Testing

**New file:** `test/llm/test_anthropic_streaming.c`

Unit tests:
1. Parse message_start (input tokens)
2. Parse content_block_start for text
3. Parse content_block_start for thinking
4. Parse content_block_start for tool_use
5. Parse content_block_delta for text
6. Parse content_block_delta for thinking
7. Parse content_block_delta for tool input
8. Parse message_delta (stop_reason, output tokens)
9. Parse error events
10. Handle ping events (ignore)

**Test fixtures:**
- `test/fixtures/anthropic_stream_text.txt`
- `test/fixtures/anthropic_stream_thinking.txt`
- `test/fixtures/anthropic_stream_tool.txt`

## Verification

1. `make clean && make`
2. `make test`
3. Manual testing with real Anthropic API:
   - Text response streams correctly
   - Thinking content displays (dimmed)
   - Tool calls show name during stream
   - Token counts accurate
4. `make check-valgrind`

## Notes

- Anthropic's event system is more complex than OpenAI's
- The `event:` line parsing is Anthropic-specific but could benefit other providers
- Thinking content is unique to Anthropic/Claude
