#ifndef OUTPUT_FORMATTER_H
#define OUTPUT_FORMATTER_H

#include <stddef.h>
#include <stdbool.h>


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


#endif /* OUTPUT_FORMATTER_H */