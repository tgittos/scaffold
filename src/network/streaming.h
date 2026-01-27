#ifndef STREAMING_H
#define STREAMING_H

#include <stddef.h>
#include <sys/types.h>  // For ssize_t
#include "../utils/darray.h"

/**
 * Streaming SSE Parser and Context Management
 *
 * This module provides the foundational infrastructure for parsing
 * Server-Sent Events (SSE) streams from LLM APIs and accumulating
 * response content (text, thinking, tool calls).
 */

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
    char* arguments_json;
    size_t arguments_capacity;
} StreamingToolUse;

DARRAY_DECLARE(StreamingToolUseArray, StreamingToolUse)

typedef struct {
    StreamState state;

    char* line_buffer;
    size_t line_buffer_len;
    size_t line_buffer_capacity;

    // SSE event type from "event:" lines, used by Anthropic's event-typed SSE protocol
    char* current_event_type;

    char* text_content;
    size_t text_len;
    size_t text_capacity;

    char* thinking_content;
    size_t thinking_len;
    size_t thinking_capacity;

    StreamingToolUseArray tool_uses;
    ssize_t current_tool_index; // -1 when no tool is active

    int input_tokens;
    int output_tokens;
    char* stop_reason;
    char* error_message;

    void (*on_text_chunk)(const char* text, size_t len, void* user_data);
    void (*on_thinking_chunk)(const char* text, size_t len, void* user_data);
    void (*on_tool_use_start)(const char* id, const char* name, void* user_data);
    void (*on_tool_use_delta)(const char* id, const char* json_delta, void* user_data);
    void (*on_stream_end)(const char* stop_reason, void* user_data);
    void (*on_error)(const char* error, void* user_data);

    // Provider-specific parsers hook this to receive raw JSON from "data:" lines
    void (*on_sse_data)(const char* data, size_t len, void* user_data);
    void* user_data;
} StreamingContext;

// =============================================================================
// Lifecycle Management
// =============================================================================

StreamingContext* streaming_context_create(void);

// Safe to call with NULL
void streaming_context_free(StreamingContext* ctx);

// =============================================================================
// SSE Parsing
// =============================================================================

// Buffers raw HTTP data and dispatches complete lines to streaming_process_sse_line.
// Handles arbitrary chunk boundaries from the HTTP layer.
int streaming_process_chunk(StreamingContext* ctx, const char* data, size_t len);

// Parses a complete SSE line (data/event/id/comment) and dispatches via on_sse_data.
int streaming_process_sse_line(StreamingContext* ctx, const char* line, size_t len);

// =============================================================================
// Event Emission (called by provider-specific parsers)
// =============================================================================

void streaming_emit_text(StreamingContext* ctx, const char* text, size_t len);
void streaming_emit_thinking(StreamingContext* ctx, const char* text, size_t len);
void streaming_emit_tool_start(StreamingContext* ctx, const char* id, const char* name);
void streaming_emit_tool_delta(StreamingContext* ctx, const char* id, const char* json_delta, size_t len);
void streaming_emit_complete(StreamingContext* ctx, const char* stop_reason);
void streaming_emit_error(StreamingContext* ctx, const char* error);

#endif /* STREAMING_H */
