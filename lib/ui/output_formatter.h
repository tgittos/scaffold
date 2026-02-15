#ifndef OUTPUT_FORMATTER_H
#define OUTPUT_FORMATTER_H

#include <stddef.h>
#include <stdbool.h>


/**
 * Set JSON output mode enabled/disabled.
 * When enabled, terminal display functions become no-ops and JSON output
 * should be used instead.
 *
 * @param enabled true to enable JSON mode, false for terminal mode
 */
void set_json_output_mode(bool enabled);

/**
 * Get current JSON output mode state.
 *
 * @return true if JSON mode is enabled, false otherwise
 */
bool get_json_output_mode(void);

typedef struct ParsedResponse {
    char *thinking_content;  /* Content inside <think> tags (optional) */
    char *response_content;  /* Actual response content */
    int prompt_tokens;
    int completion_tokens;
    int total_tokens;
} ParsedResponse;

/**
 * Parse JSON response from OpenAI/LLM API and extract message content and token usage
 *
 * @param json_response Raw JSON response string
 * @param result Pointer to ParsedResponse struct to store results
 * @return 0 on success, -1 on error
 *
 * Note: Caller must free result->thinking_content and result->response_content when done
 */
int parse_api_response(const char *json_response, ParsedResponse *result);

/**
 * Parse JSON response with model-specific capabilities
 *
 * @param json_response Raw JSON response string
 * @param model_name Name of the model being used
 * @param result Pointer to ParsedResponse struct to store results
 * @return 0 on success, -1 on error
 *
 * Note: Caller must free result->thinking_content and result->response_content when done
 */
int parse_api_response_with_model(const char *json_response, const char *model_name, ParsedResponse *result);

/**
 * Parse JSON response from Anthropic API and extract message content and token usage
 *
 * @param json_response Raw JSON response string
 * @param result Pointer to ParsedResponse struct to store results
 * @return 0 on success, -1 on error
 *
 * Note: Caller must free result->thinking_content and result->response_content when done
 */
int parse_anthropic_response(const char *json_response, ParsedResponse *result);

/**
 * Clean up memory allocated in ParsedResponse
 *
 * @param response Pointer to ParsedResponse struct to clean up
 */
void cleanup_parsed_response(ParsedResponse *response);

/**
 * Print formatted response with improved visual grouping and separation
 *
 * @param response Parsed response struct
 */
void print_formatted_response_improved(const ParsedResponse *response);


/**
 * Log tool execution with improved formatting and grouping
 *
 * @param tool_name Name of the tool
 * @param arguments Tool arguments in JSON format
 * @param success Whether the tool execution was successful
 * @param result Result of the tool execution
 */
void log_tool_execution_improved(const char *tool_name, const char *arguments, bool success, const char *result);

/**
 * Extract a summary string from tool arguments for display.
 * Returns a newly allocated string (caller must free) or NULL.
 *
 * @param tool_name Name of the tool (used for context-specific extraction)
 * @param arguments JSON string of tool arguments
 * @return Allocated summary string or NULL
 */
char *extract_arg_summary(const char *tool_name, const char *arguments);

/**
 * Display start of system info group with visual separator
 */
void display_system_info_group_start(void);

/**
 * Display end of system info group with visual separator
 */
void display_system_info_group_end(void);

/**
 * Log system information with improved formatting and grouping
 *
 * @param category Category of the system info
 * @param message The info message
 */
void log_system_info(const char *category, const char *message);

/**
 * Cleanup output formatter resources including the global model registry.
 * Should be called during application shutdown.
 */
void cleanup_output_formatter(void);

/**
 * Initialize streaming display mode
 * Clears any "thinking" indicator and prepares for streaming output
 */
void display_streaming_init(void);

/**
 * Display streaming text content as it arrives
 *
 * @param text The text chunk to display
 * @param len Length of the text chunk
 */
void display_streaming_text(const char* text, size_t len);

/**
 * Display streaming thinking content (dimmed/gray)
 *
 * @param text The thinking text chunk to display
 * @param len Length of the text chunk
 */
void display_streaming_thinking(const char* text, size_t len);

/**
 * Display notification that a tool is being called
 *
 * @param id Tool call ID from the LLM response
 * @param tool_name Name of the tool being called
 */
void display_streaming_tool_start(const char* id, const char* tool_name);

/**
 * Display streaming tool input JSON as it arrives
 *
 * @param id Tool call ID this delta belongs to
 * @param json_delta Chunk of JSON input for the tool
 * @param len Length of the JSON chunk
 */
void display_streaming_tool_delta(const char* id, const char* json_delta, size_t len);

/**
 * Display result of a tool execution
 *
 * @param id Tool call ID that produced this result
 * @param name Name of the tool
 * @param arguments Tool arguments JSON (for terminal display summary)
 * @param result Result string from tool execution
 * @param success Whether the tool execution succeeded
 */
void display_streaming_tool_result(const char* id, const char* name,
                                   const char* arguments, const char* result,
                                   int success);

/**
 * Display completion of streaming response with token counts
 *
 * @param input_tokens Number of input/prompt tokens used
 * @param output_tokens Number of output/completion tokens generated
 */
void display_streaming_complete(int input_tokens, int output_tokens);

/**
 * Display an error during streaming
 *
 * @param error Error message to display
 */
void display_streaming_error(const char* error);

/**
 * Display message notification indicator (yellow dot with count)
 *
 * @param count Number of pending messages
 */
void display_message_notification(int count);

/**
 * Clear the message notification indicator
 */
void display_message_notification_clear(void);

/**
 * Log a subagent approval request with visual deprioritization.
 *
 * @param subagent_id The 16-char hex ID of the subagent (abbreviated in display)
 * @param tool_name Name of the tool that required approval
 * @param display_summary Human-readable summary of the operation
 * @param result The approval decision (requires ApprovalResult from approval_gate.h)
 */
void log_subagent_approval(const char *subagent_id,
                           const char *tool_name,
                           const char *display_summary,
                           int result);

/**
 * Display a cancellation message when an operation is interrupted by Ctrl+C.
 *
 * @param tools_completed Number of tools that completed before cancellation
 * @param tools_total Total number of tools that were requested
 * @param json_mode Whether to output in JSON format
 */
void display_cancellation_message(int tools_completed, int tools_total, bool json_mode);

/**
 * Display notification that subagents were launched.
 * Shows tree-style listing of agent tasks.
 *
 * @param count Number of agents launched
 * @param tasks Array of task description strings
 */
void display_agents_launched(int count, const char **tasks);

/**
 * Display notification that a subagent completed.
 *
 * @param task Task description of the completed agent
 * @param elapsed_secs Seconds the agent ran
 * @param success Whether the agent completed successfully
 */
void display_agent_completed(const char *task, int elapsed_secs, bool success);

#endif /* OUTPUT_FORMATTER_H */
