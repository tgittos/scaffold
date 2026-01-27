#include "streaming.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Define the StreamingToolUseArray implementation
DARRAY_DEFINE(StreamingToolUseArray, StreamingToolUse)

// Initial buffer capacities
#define INITIAL_LINE_BUFFER_CAPACITY 1024
#define INITIAL_TEXT_CAPACITY 4096
#define INITIAL_THINKING_CAPACITY 2048
#define INITIAL_TOOL_CAPACITY 4
#define INITIAL_TOOL_ARGS_CAPACITY 1024

// Growth factor for buffer reallocation
#define BUFFER_GROWTH_FACTOR 2

// =============================================================================
// Internal Helper Functions
// =============================================================================

/**
 * Ensure buffer has capacity for at least additional_len more bytes
 * Returns 0 on success, -1 on allocation failure or overflow
 */
static int ensure_capacity(char** buffer, size_t* capacity, size_t current_len, size_t additional_len) {
    if (buffer == NULL || capacity == NULL) {
        return -1;
    }

    // Check for overflow in needed calculation
    if (additional_len > SIZE_MAX - current_len - 1) {
        return -1;  // Would overflow
    }
    size_t needed = current_len + additional_len + 1;  // +1 for null terminator

    if (needed <= *capacity) {
        return 0;  // Already have enough capacity
    }

    // Calculate new capacity with growth factor, checking for overflow
    size_t new_capacity = *capacity;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / BUFFER_GROWTH_FACTOR) {
            return -1;  // Would overflow
        }
        new_capacity *= BUFFER_GROWTH_FACTOR;
    }

    char* new_buffer = realloc(*buffer, new_capacity);
    if (new_buffer == NULL) {
        return -1;
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return 0;
}

/**
 * Append a single character to the line buffer
 */
static int streaming_append_char(StreamingContext* ctx, char c) {
    if (ctx == NULL) {
        return -1;
    }

    if (ensure_capacity(&ctx->line_buffer, &ctx->line_buffer_capacity,
                       ctx->line_buffer_len, 1) != 0) {
        return -1;
    }

    ctx->line_buffer[ctx->line_buffer_len++] = c;
    ctx->line_buffer[ctx->line_buffer_len] = '\0';
    return 0;
}

/**
 * Free a single StreamingToolUse
 */
static void free_tool_use(StreamingToolUse* tool) {
    if (tool == NULL) {
        return;
    }
    free(tool->id);
    tool->id = NULL;
    free(tool->name);
    tool->name = NULL;
    free(tool->arguments_json);
    tool->arguments_json = NULL;
    tool->arguments_capacity = 0;
}

// =============================================================================
// Lifecycle Management
// =============================================================================

StreamingContext* streaming_context_create(void) {
    StreamingContext* ctx = calloc(1, sizeof(StreamingContext));
    if (ctx == NULL) {
        return NULL;
    }

    // Initialize state
    ctx->state = STREAM_STATE_IDLE;
    ctx->current_tool_index = -1;

    // Allocate line buffer
    ctx->line_buffer = malloc(INITIAL_LINE_BUFFER_CAPACITY);
    if (ctx->line_buffer == NULL) {
        streaming_context_free(ctx);
        return NULL;
    }
    ctx->line_buffer[0] = '\0';
    ctx->line_buffer_len = 0;
    ctx->line_buffer_capacity = INITIAL_LINE_BUFFER_CAPACITY;

    // Allocate text content buffer
    ctx->text_content = malloc(INITIAL_TEXT_CAPACITY);
    if (ctx->text_content == NULL) {
        streaming_context_free(ctx);
        return NULL;
    }
    ctx->text_content[0] = '\0';
    ctx->text_len = 0;
    ctx->text_capacity = INITIAL_TEXT_CAPACITY;

    // Allocate thinking content buffer
    ctx->thinking_content = malloc(INITIAL_THINKING_CAPACITY);
    if (ctx->thinking_content == NULL) {
        streaming_context_free(ctx);
        return NULL;
    }
    ctx->thinking_content[0] = '\0';
    ctx->thinking_len = 0;
    ctx->thinking_capacity = INITIAL_THINKING_CAPACITY;

    // Initialize tool uses array
    if (StreamingToolUseArray_init_capacity(&ctx->tool_uses, INITIAL_TOOL_CAPACITY) != 0) {
        streaming_context_free(ctx);
        return NULL;
    }

    // Initialize metadata
    ctx->input_tokens = 0;
    ctx->output_tokens = 0;
    ctx->stop_reason = NULL;
    ctx->error_message = NULL;

    // Initialize SSE event type tracking
    ctx->current_event_type = NULL;

    // Callbacks start as NULL
    ctx->on_text_chunk = NULL;
    ctx->on_thinking_chunk = NULL;
    ctx->on_tool_use_start = NULL;
    ctx->on_tool_use_delta = NULL;
    ctx->on_stream_end = NULL;
    ctx->on_error = NULL;
    ctx->on_sse_data = NULL;
    ctx->user_data = NULL;

    return ctx;
}

void streaming_context_free(StreamingContext* ctx) {
    if (ctx == NULL) {
        return;
    }

    // Free line buffer
    free(ctx->line_buffer);
    ctx->line_buffer = NULL;

    // Free text content
    free(ctx->text_content);
    ctx->text_content = NULL;

    // Free thinking content
    free(ctx->thinking_content);
    ctx->thinking_content = NULL;

    // Free tool uses
    for (size_t i = 0; i < ctx->tool_uses.count; i++) {
        free_tool_use(&ctx->tool_uses.data[i]);
    }
    StreamingToolUseArray_destroy(&ctx->tool_uses);

    // Free metadata strings
    free(ctx->stop_reason);
    ctx->stop_reason = NULL;
    free(ctx->error_message);
    ctx->error_message = NULL;

    // Free SSE event type
    free(ctx->current_event_type);
    ctx->current_event_type = NULL;

    free(ctx);
}

// =============================================================================
// SSE Parsing
// =============================================================================

int streaming_process_chunk(StreamingContext* ctx, const char* data, size_t len) {
    if (ctx == NULL || data == NULL) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        if (c == '\n') {
            // Process accumulated line if non-empty
            if (ctx->line_buffer_len > 0) {
                if (streaming_process_sse_line(ctx, ctx->line_buffer, ctx->line_buffer_len) != 0) {
                    return -1;
                }
            }
            // Reset line buffer for next line
            ctx->line_buffer_len = 0;
            ctx->line_buffer[0] = '\0';
        } else if (c != '\r') {
            // Accumulate into line buffer (skip carriage returns)
            if (streaming_append_char(ctx, c) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

int streaming_process_sse_line(StreamingContext* ctx, const char* line, size_t len) {
    if (ctx == NULL || line == NULL || len == 0) {
        return 0;  // Empty line is valid (event boundary)
    }

    // Check for "data: " prefix
    if (len >= 6 && strncmp(line, "data: ", 6) == 0) {
        const char* payload = line + 6;
        size_t payload_len = len - 6;

        // Check for "[DONE]" termination signal
        if (payload_len == 6 && strncmp(payload, "[DONE]", 6) == 0) {
            streaming_emit_complete(ctx, ctx->stop_reason ? ctx->stop_reason : "complete");
            return 0;
        }

        // Update state to indicate we're reading data
        ctx->state = STREAM_STATE_READING_DATA;

        // Invoke SSE data callback for provider-specific parsing
        if (ctx->on_sse_data != NULL && payload_len > 0) {
            ctx->on_sse_data(payload, payload_len, ctx->user_data);
        }

        return 0;
    }

    // Check for "event: " prefix
    if (len >= 7 && strncmp(line, "event: ", 7) == 0) {
        ctx->state = STREAM_STATE_READING_EVENT;
        // Store event type for provider-specific handling (e.g., Anthropic)
        free(ctx->current_event_type);
        const char* event_start = line + 7;
        size_t event_len = len - 7;
        // Trim trailing whitespace
        while (event_len > 0 && (event_start[event_len - 1] == ' ' || event_start[event_len - 1] == '\t')) {
            event_len--;
        }
        ctx->current_event_type = strndup(event_start, event_len);
        return 0;
    }

    // Check for "id: " prefix
    if (len >= 4 && strncmp(line, "id: ", 4) == 0) {
        // SSE id field - can be used for reconnection
        // Not currently used but parsed for completeness
        return 0;
    }

    // Comment lines start with ':'
    if (line[0] == ':') {
        return 0;  // Ignore comments
    }

    return 0;
}

// =============================================================================
// Event Emission
// =============================================================================

void streaming_emit_text(StreamingContext* ctx, const char* text, size_t len) {
    if (ctx == NULL || text == NULL || len == 0) {
        return;
    }

    // Ensure capacity for new text
    if (ensure_capacity(&ctx->text_content, &ctx->text_capacity,
                       ctx->text_len, len) != 0) {
        ctx->state = STREAM_STATE_ERROR;
        return;  // Allocation failed
    }

    // Append text
    memcpy(ctx->text_content + ctx->text_len, text, len);
    ctx->text_len += len;
    ctx->text_content[ctx->text_len] = '\0';

    // Invoke callback if set
    if (ctx->on_text_chunk != NULL) {
        ctx->on_text_chunk(text, len, ctx->user_data);
    }
}

void streaming_emit_thinking(StreamingContext* ctx, const char* text, size_t len) {
    if (ctx == NULL || text == NULL || len == 0) {
        return;
    }

    // Ensure capacity for new thinking text
    if (ensure_capacity(&ctx->thinking_content, &ctx->thinking_capacity,
                       ctx->thinking_len, len) != 0) {
        ctx->state = STREAM_STATE_ERROR;
        return;  // Allocation failed
    }

    // Append thinking text
    memcpy(ctx->thinking_content + ctx->thinking_len, text, len);
    ctx->thinking_len += len;
    ctx->thinking_content[ctx->thinking_len] = '\0';

    // Invoke callback if set
    if (ctx->on_thinking_chunk != NULL) {
        ctx->on_thinking_chunk(text, len, ctx->user_data);
    }
}

void streaming_emit_tool_start(StreamingContext* ctx, const char* id, const char* name) {
    if (ctx == NULL || id == NULL || name == NULL) {
        return;
    }

    // Initialize new tool use
    StreamingToolUse new_tool = {0};
    new_tool.id = strdup(id);
    new_tool.name = strdup(name);
    new_tool.arguments_json = malloc(INITIAL_TOOL_ARGS_CAPACITY);

    if (new_tool.id == NULL ||
        new_tool.name == NULL ||
        new_tool.arguments_json == NULL) {
        // Allocation failed, clean up
        free(new_tool.id);
        free(new_tool.name);
        free(new_tool.arguments_json);
        return;
    }

    new_tool.arguments_json[0] = '\0';
    new_tool.arguments_capacity = INITIAL_TOOL_ARGS_CAPACITY;

    // Add to array
    if (StreamingToolUseArray_push(&ctx->tool_uses, new_tool) != 0) {
        // Push failed, clean up
        free(new_tool.id);
        free(new_tool.name);
        free(new_tool.arguments_json);
        return;
    }

    ctx->current_tool_index = (ssize_t)(ctx->tool_uses.count - 1);

    // Invoke callback if set
    if (ctx->on_tool_use_start != NULL) {
        ctx->on_tool_use_start(id, name, ctx->user_data);
    }
}

void streaming_emit_tool_delta(StreamingContext* ctx, const char* id, const char* json_delta, size_t len) {
    if (ctx == NULL || id == NULL || json_delta == NULL || len == 0) {
        return;
    }

    // Find the tool with matching ID
    ssize_t idx = ctx->current_tool_index;
    if (idx < 0 || (size_t)idx >= ctx->tool_uses.count) {
        // Search for matching tool
        for (size_t i = 0; i < ctx->tool_uses.count; i++) {
            if (ctx->tool_uses.data[i].id != NULL && strcmp(ctx->tool_uses.data[i].id, id) == 0) {
                idx = (ssize_t)i;
                ctx->current_tool_index = (ssize_t)i;
                break;
            }
        }
    }

    if (idx < 0 || (size_t)idx >= ctx->tool_uses.count) {
        return;  // Tool not found
    }

    StreamingToolUse* tool = &ctx->tool_uses.data[idx];

    // Verify ID matches
    if (tool->id == NULL || strcmp(tool->id, id) != 0) {
        return;  // ID mismatch
    }

    // Calculate current length
    size_t current_len = strlen(tool->arguments_json);

    // Ensure capacity
    size_t needed = current_len + len + 1;
    if (needed > tool->arguments_capacity) {
        size_t new_capacity = tool->arguments_capacity;
        while (new_capacity < needed) {
            if (new_capacity > SIZE_MAX / BUFFER_GROWTH_FACTOR) {
                ctx->state = STREAM_STATE_ERROR;
                return;  // Would overflow
            }
            new_capacity *= BUFFER_GROWTH_FACTOR;
        }
        char* new_buffer = realloc(tool->arguments_json, new_capacity);
        if (new_buffer == NULL) {
            ctx->state = STREAM_STATE_ERROR;
            return;  // Allocation failed
        }
        tool->arguments_json = new_buffer;
        tool->arguments_capacity = new_capacity;
    }

    // Append delta
    memcpy(tool->arguments_json + current_len, json_delta, len);
    tool->arguments_json[current_len + len] = '\0';

    // Invoke callback if set
    if (ctx->on_tool_use_delta != NULL) {
        ctx->on_tool_use_delta(id, json_delta, ctx->user_data);
    }
}

void streaming_emit_complete(StreamingContext* ctx, const char* stop_reason) {
    if (ctx == NULL) {
        return;
    }

    ctx->state = STREAM_STATE_COMPLETE;

    // Store stop reason if provided
    if (stop_reason != NULL) {
        free(ctx->stop_reason);
        ctx->stop_reason = strdup(stop_reason);
    }

    // Invoke callback if set
    if (ctx->on_stream_end != NULL) {
        ctx->on_stream_end(stop_reason, ctx->user_data);
    }
}

void streaming_emit_error(StreamingContext* ctx, const char* error) {
    if (ctx == NULL) {
        return;
    }

    ctx->state = STREAM_STATE_ERROR;

    // Store error message if provided
    if (error != NULL) {
        free(ctx->error_message);
        ctx->error_message = strdup(error);
    }

    // Invoke callback if set
    if (ctx->on_error != NULL) {
        ctx->on_error(error, ctx->user_data);
    }
}
