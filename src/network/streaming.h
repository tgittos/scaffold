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

/**
 * Represents an in-progress tool use being accumulated from stream deltas
 */
typedef struct {
    char* id;
    char* name;
    char* arguments_json;      // Accumulated from partial deltas
    size_t arguments_capacity;
} StreamingToolUse;

/**
 * Dynamic array of StreamingToolUse
 */
DARRAY_DECLARE(StreamingToolUseArray, StreamingToolUse)

/**
 * Context for an active streaming response
 *
 * Maintains all state needed to parse SSE events and accumulate
 * the response content. Supports optional callbacks for real-time
 * display of streaming content.
 */
typedef struct {
    StreamState state;

    // SSE line buffering
    char* line_buffer;
    size_t line_buffer_len;
    size_t line_buffer_capacity;

    // SSE event type (from "event: xxx" line, for providers like Anthropic)
    char* current_event_type;

    // Accumulated text content
    char* text_content;
    size_t text_len;
    size_t text_capacity;

    // Accumulated thinking content (extended thinking)
    char* thinking_content;
    size_t thinking_len;
    size_t thinking_capacity;

    // Tool calls accumulated from stream
    StreamingToolUseArray tool_uses;

    // Current tool index for delta accumulation (-1 when not set)
    ssize_t current_tool_index;

    // Response metadata
    int input_tokens;
    int output_tokens;
    char* stop_reason;
    char* error_message;

    // Optional callbacks for real-time display
    void (*on_text_chunk)(const char* text, size_t len, void* user_data);
    void (*on_thinking_chunk)(const char* text, size_t len, void* user_data);
    void (*on_tool_use_start)(const char* id, const char* name, void* user_data);
    void (*on_tool_use_delta)(const char* id, const char* json_delta, void* user_data);
    void (*on_stream_end)(const char* stop_reason, void* user_data);
    void (*on_error)(const char* error, void* user_data);

    // Callback for raw SSE data events (for provider-specific JSON parsing)
    void (*on_sse_data)(const char* data, size_t len, void* user_data);
    void* user_data;
} StreamingContext;

// =============================================================================
// Lifecycle Management
// =============================================================================

/**
 * Create a new streaming context with default initial capacities
 *
 * @return Newly allocated StreamingContext, or NULL on allocation failure
 */
StreamingContext* streaming_context_create(void);

/**
 * Free a streaming context and all associated memory
 *
 * @param ctx The context to free (safe to call with NULL)
 */
void streaming_context_free(StreamingContext* ctx);

// =============================================================================
// SSE Parsing
// =============================================================================

/**
 * Process a chunk of SSE data from the HTTP stream
 *
 * Buffers incomplete lines and calls streaming_process_sse_line
 * for each complete line. This handles the case where SSE data
 * may arrive in arbitrary chunk boundaries.
 *
 * @param ctx The streaming context
 * @param data Raw data from HTTP callback
 * @param len Length of data
 * @return 0 on success, -1 on error
 */
int streaming_process_chunk(StreamingContext* ctx, const char* data, size_t len);

/**
 * Process a complete SSE line
 *
 * Parses lines like:
 *   - "data: {...JSON...}"
 *   - "data: [DONE]"
 *   - "event: message_start"
 *   - "id: 123"
 *
 * For data lines, extracts the JSON payload for provider-specific parsing.
 *
 * @param ctx The streaming context
 * @param line The complete line (without trailing newline)
 * @param len Length of the line
 * @return 0 on success, -1 on error
 */
int streaming_process_sse_line(StreamingContext* ctx, const char* line, size_t len);

// =============================================================================
// Event Emission (called by provider-specific parsers)
// =============================================================================

/**
 * Emit accumulated text content
 *
 * Appends text to the context's text_content buffer and invokes
 * the on_text_chunk callback if set.
 *
 * @param ctx The streaming context
 * @param text Text to append
 * @param len Length of text
 */
void streaming_emit_text(StreamingContext* ctx, const char* text, size_t len);

/**
 * Emit accumulated thinking content (extended thinking)
 *
 * Appends text to the context's thinking_content buffer and invokes
 * the on_thinking_chunk callback if set.
 *
 * @param ctx The streaming context
 * @param text Thinking text to append
 * @param len Length of text
 */
void streaming_emit_thinking(StreamingContext* ctx, const char* text, size_t len);

/**
 * Signal the start of a new tool use
 *
 * Creates a new StreamingToolUse entry and invokes the on_tool_use_start
 * callback if set.
 *
 * @param ctx The streaming context
 * @param id Tool use ID
 * @param name Tool name
 */
void streaming_emit_tool_start(StreamingContext* ctx, const char* id, const char* name);

/**
 * Append a JSON delta to the current tool's arguments
 *
 * Appends the JSON fragment to the current tool's arguments_json buffer
 * and invokes the on_tool_use_delta callback if set.
 *
 * @param ctx The streaming context
 * @param id Tool use ID (for validation)
 * @param json_delta JSON fragment to append
 * @param len Length of JSON fragment
 */
void streaming_emit_tool_delta(StreamingContext* ctx, const char* id, const char* json_delta, size_t len);

/**
 * Signal stream completion
 *
 * Sets the context state to STREAM_STATE_COMPLETE and invokes
 * the on_stream_end callback if set.
 *
 * @param ctx The streaming context
 * @param stop_reason The reason for stopping (e.g., "end_turn", "tool_use")
 */
void streaming_emit_complete(StreamingContext* ctx, const char* stop_reason);

/**
 * Signal a stream error
 *
 * Sets the context state to STREAM_STATE_ERROR and invokes
 * the on_error callback if set.
 *
 * @param ctx The streaming context
 * @param error Error message
 */
void streaming_emit_error(StreamingContext* ctx, const char* error);

#endif /* STREAMING_H */
