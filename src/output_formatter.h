#ifndef OUTPUT_FORMATTER_H
#define OUTPUT_FORMATTER_H

#include <stddef.h>

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

#endif /* OUTPUT_FORMATTER_H */