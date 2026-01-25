#ifndef OUTPUT_FORMATTER_H
#define OUTPUT_FORMATTER_H

#include <stddef.h>
#include <stdbool.h>


// =============================================================================
// JSON Output Mode Control
// =============================================================================

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

// ANSI color codes
#define ANSI_RESET   "\033[0m"
#define ANSI_GRAY    "\033[90m"
#define ANSI_DIM     "\033[2m"
#define ANSI_BLUE    "\033[34m"

typedef struct {
    char *thinking_content;  // Content inside <think> tags (optional)
    char *response_content;  // Actual response content
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
 * Format and print the parsed response with content and token usage
 * Content is printed prominently, token usage is visually de-prioritized
 * 
 * @param response Parsed response struct
 */
void print_formatted_response(const ParsedResponse *response);

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
 * Display start of tool execution group with visual separator
 */
void display_tool_execution_group_start(void);

/**
 * Display end of tool execution group with visual separator
 */
void display_tool_execution_group_end(void);

/**
 * Check if a tool execution group is currently active
 * Useful for suppressing other output that would interleave with the tool box
 *
 * @return true if tool execution group is active, false otherwise
 */
bool is_tool_execution_group_active(void);

/**
 * Print a line of content inside the tool execution box with proper borders
 * The line will be padded to fit within the 80-character box width
 *
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void print_tool_box_line(const char* format, ...);

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

// =============================================================================
// Streaming Display Functions
// =============================================================================

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
 * @param tool_name Name of the tool being called
 */
void display_streaming_tool_start(const char* tool_name);

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

#endif /* OUTPUT_FORMATTER_H */