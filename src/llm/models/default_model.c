#include "model_capabilities.h"
#include "output_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Default response processor for models without thinking support
static int default_process_response(const char* content, ParsedResponse* result) {
    if (!content || !result) {
        return -1;
    }
    
    // Initialize result
    result->thinking_content = NULL;
    result->response_content = NULL;
    
    // For default models, entire content is the response (no thinking support)
    size_t content_len = strlen(content);
    result->response_content = malloc(content_len + 1);
    if (!result->response_content) {
        return -1;
    }
    strcpy(result->response_content, content);
    
    return 0;
}

// Default model capabilities
static ModelCapabilities default_model = {
    .model_pattern = "default",
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = default_process_response,
    .supports_function_calling = 0,
    .generate_tools_json = NULL,
    .parse_tool_calls = NULL,
    .format_tool_result_message = NULL,
    .format_assistant_tool_message = NULL,
    .supports_structured_output = 0,
    .supports_json_mode = 0,
    .max_context_length = 4096
};

int register_default_model(ModelRegistry* registry) {
    return register_model_capabilities(registry, &default_model);
}