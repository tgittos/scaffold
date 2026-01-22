#ifndef JSON_OUTPUT_H
#define JSON_OUTPUT_H

#include <stdbool.h>
#include "../network/streaming.h"
#include "tools_system.h"

/**
 * Initialize the JSON output system.
 * Sets up any state needed for JSON output mode.
 */
void json_output_init(void);

/**
 * Output an assistant text response in JSON format.
 *
 * @param text The assistant's text response
 * @param input_tokens Number of input tokens used
 * @param output_tokens Number of output tokens generated
 */
void json_output_assistant_text(const char* text, int input_tokens, int output_tokens);

/**
 * Output assistant tool calls in JSON format (streaming context).
 *
 * @param tools Array of streaming tool uses
 * @param count Number of tool calls
 * @param input_tokens Number of input tokens used
 * @param output_tokens Number of output tokens generated
 */
void json_output_assistant_tool_calls(StreamingToolUse* tools, int count, int input_tokens, int output_tokens);

/**
 * Output assistant tool calls in JSON format (buffered/loop context).
 *
 * @param tool_calls Array of tool calls
 * @param count Number of tool calls
 * @param input_tokens Number of input tokens used
 * @param output_tokens Number of output tokens generated
 */
void json_output_assistant_tool_calls_buffered(ToolCall* tool_calls, int count, int input_tokens, int output_tokens);

/**
 * Output a tool result in JSON format.
 *
 * @param tool_use_id The ID of the tool call this is a result for
 * @param content The tool result content
 * @param is_error Whether this is an error result
 */
void json_output_tool_result(const char* tool_use_id, const char* content, bool is_error);

/**
 * Output a system message in JSON format.
 *
 * @param subtype The subtype of system message (e.g., "info", "warning")
 * @param message The system message content
 */
void json_output_system(const char* subtype, const char* message);

/**
 * Output an error message in JSON format.
 *
 * @param error The error message
 */
void json_output_error(const char* error);

/**
 * Output a final result in JSON format.
 *
 * @param result The final result text
 */
void json_output_result(const char* result);

#endif /* JSON_OUTPUT_H */
