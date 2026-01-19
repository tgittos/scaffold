# Plan C: Streaming Configuration & Polish

## Prerequisites

- Plan A complete (streaming foundation)
- Plan B complete (OpenAI streaming)

## Goal

Add configuration options for streaming behavior, improve error handling, and polish the user experience.

## Deliverables

### 1. Configuration Options

**Modify:** `src/utils/config.h`

```c
struct RalphConfig {
    // ... existing fields ...

    // Streaming options
    bool enable_streaming;              // Default: true
    int streaming_timeout_first_byte;   // ms, default: 30000
    int streaming_timeout_between;      // ms, default: 10000
    bool show_token_count;              // Show tokens after response, default: false
};
```

**Modify:** `src/utils/config.c`

```c
static void config_set_defaults(RalphConfig* config) {
    // ... existing defaults ...

    config->enable_streaming = true;
    config->streaming_timeout_first_byte = 30000;
    config->streaming_timeout_between = 10000;
    config->show_token_count = false;
}

static int config_parse_field(RalphConfig* config, const char* key, const char* value) {
    // ... existing parsing ...

    if (strcmp(key, "enable_streaming") == 0) {
        config->enable_streaming = parse_bool(value);
    } else if (strcmp(key, "streaming_timeout_first_byte") == 0) {
        config->streaming_timeout_first_byte = atoi(value);
    } else if (strcmp(key, "streaming_timeout_between") == 0) {
        config->streaming_timeout_between = atoi(value);
    } else if (strcmp(key, "show_token_count") == 0) {
        config->show_token_count = parse_bool(value);
    }
    // ...
}
```

### 2. CLI Flag Support

**Modify:** `src/main.c` or argument parsing

```c
// Add --no-stream flag
{"no-stream", no_argument, NULL, 'S'},

case 'S':
    config->enable_streaming = false;
    break;
```

### 3. Graceful Fallback

**Modify:** `src/core/ralph.c`

```c
int ralph_process_message(RalphSession* session, const char* user_message) {
    RalphConfig* config = session->config;
    LLMProvider* provider = session->provider;

    bool use_streaming = config->enable_streaming
                         && provider->supports_streaming
                         && provider->supports_streaming(provider);

    if (use_streaming) {
        int result = ralph_process_message_streaming(session, user_message);

        // If streaming fails, fall back to buffered mode
        if (result == STREAM_ERROR_TIMEOUT || result == STREAM_ERROR_PARSE) {
            fprintf(stderr, "Streaming failed, falling back to buffered mode\n");
            return ralph_process_message_buffered(session, user_message);
        }
        return result;
    }

    return ralph_process_message_buffered(session, user_message);
}
```

### 4. Improved Error Handling

**Modify:** `src/network/streaming.c`

```c
typedef enum {
    STREAM_OK = 0,
    STREAM_ERROR_TIMEOUT = -1,
    STREAM_ERROR_PARSE = -2,
    STREAM_ERROR_NETWORK = -3,
    STREAM_ERROR_PROVIDER = -4,  // Provider returned error in stream
} StreamResult;

// Handle provider errors embedded in stream
int streaming_check_for_error(StreamingContext* ctx, const char* json_data, size_t len) {
    cJSON* root = cJSON_ParseWithLength(json_data, len);
    if (!root) return 0;

    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON* message = cJSON_GetObjectItem(error, "message");
        if (message && cJSON_IsString(message)) {
            streaming_emit_error(ctx, message->valuestring);
            cJSON_Delete(root);
            return STREAM_ERROR_PROVIDER;
        }
    }

    cJSON_Delete(root);
    return 0;
}
```

### 5. Token Count Display

**Modify:** `src/utils/output_formatter.c`

```c
void display_streaming_complete(int input_tokens, int output_tokens, bool show_tokens) {
    printf("\n");

    if (show_tokens && (input_tokens > 0 || output_tokens > 0)) {
        printf("\033[90m[%d in / %d out tokens]\033[0m\n", input_tokens, output_tokens);
    }

    fflush(stdout);
}
```

### 6. Interrupt Handling

**Modify:** `src/core/ralph.c`

Handle Ctrl+C during streaming:

```c
static volatile sig_atomic_t stream_interrupted = 0;

static void stream_interrupt_handler(int sig) {
    stream_interrupted = 1;
}

static size_t ralph_stream_callback(const char* data, size_t len, void* user_data) {
    if (stream_interrupted) {
        return 0; // Abort stream by returning 0
    }

    StreamingContext* ctx = (StreamingContext*)user_data;
    return streaming_process_chunk(ctx, data, len);
}

int ralph_process_message_streaming(RalphSession* session, const char* user_message) {
    // Setup interrupt handler
    struct sigaction sa, old_sa;
    sa.sa_handler = stream_interrupt_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &old_sa);
    stream_interrupted = 0;

    // ... streaming code ...

    // Restore old handler
    sigaction(SIGINT, &old_sa, NULL);

    if (stream_interrupted) {
        printf("\n[Interrupted]\n");
        // Still add partial response to conversation
        if (ctx->text_len > 0) {
            ralph_add_assistant_message(session, ctx->text_content, NULL, 0);
        }
    }

    // ...
}
```

## Files Summary

| File | Action |
|------|--------|
| `src/utils/config.h` | MODIFY - add streaming config fields |
| `src/utils/config.c` | MODIFY - parse streaming config |
| `src/main.c` | MODIFY - add --no-stream flag |
| `src/core/ralph.c` | MODIFY - config-aware streaming, fallback, interrupt |
| `src/network/streaming.c` | MODIFY - error codes, error detection |
| `src/utils/output_formatter.c` | MODIFY - token display |

## Testing

**Modify:** `test/utils/test_config.c`

- Test parsing streaming config options
- Test default values

**New tests in:** `test/core/test_ralph_streaming.c`

- Test streaming disabled via config
- Test fallback on streaming error
- Test interrupt handling (if testable)

## Verification

1. `make clean && make`
2. `make test`
3. Manual testing:
   - `ralph --no-stream` uses buffered mode
   - Config file `enable_streaming: false` works
   - Ctrl+C during stream stops cleanly
   - Token counts display when configured
4. `make check-valgrind`

## Out of Scope

- Anthropic streaming (Plan D)
