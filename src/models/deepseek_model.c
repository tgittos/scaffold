#include "../model_capabilities.h"
#include "../output_formatter.h"
#include "../tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External functions for OpenAI-style tool handling
extern char* generate_tools_json(const ToolRegistry *registry);
extern int parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count);
extern char* generate_single_tool_message(const ToolResult *result);

// DeepSeek uses the same thinking tag format as Qwen
static int deepseek_process_response(const char* content, ParsedResponse* result) {
    if (!content || !result) {
        return -1;
    }
    
    // Initialize result
    result->thinking_content = NULL;
    result->response_content = NULL;
    
    // Look for <think> and </think> tags
    const char* think_start = strstr(content, "<think>");
    const char* think_end = strstr(content, "</think>");
    
    if (think_start && think_end && think_end > think_start) {
        // Extract thinking content
        think_start += 7; // Skip "<think>"
        size_t think_len = (size_t)(think_end - think_start);
        result->thinking_content = malloc(think_len + 1);
        if (!result->thinking_content) {
            return -1;
        }
        memcpy(result->thinking_content, think_start, think_len);
        result->thinking_content[think_len] = '\0';
        
        // Extract response content (everything after </think>)
        const char* response_start = think_end + 8; // Skip "</think>"
        
        // Skip leading whitespace
        while (*response_start && (*response_start == ' ' || *response_start == '\t' || 
               *response_start == '\n' || *response_start == '\r')) {
            response_start++;
        }
        
        if (*response_start) {
            size_t response_len = strlen(response_start);
            result->response_content = malloc(response_len + 1);
            if (!result->response_content) {
                free(result->thinking_content);
                result->thinking_content = NULL;
                return -1;
            }
            strcpy(result->response_content, response_start);
        }
    } else {
        // No thinking tags, entire content is the response
        size_t content_len = strlen(content);
        result->response_content = malloc(content_len + 1);
        if (!result->response_content) {
            return -1;
        }
        strcpy(result->response_content, content);
    }
    
    return 0;
}

// DeepSeek model capabilities
static ModelCapabilities deepseek_model = {
    .model_pattern = "deepseek",
    .supports_thinking_tags = 1,
    .thinking_start_tag = "<think>",
    .thinking_end_tag = "</think>",
    .process_response = deepseek_process_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = NULL,  // Use default OpenAI-style formatting
    .supports_structured_output = 0,
    .supports_json_mode = 0,
    .max_context_length = 128000
};

int register_deepseek_models(ModelRegistry* registry) {
    return register_model_capabilities(registry, &deepseek_model);
}