#include "model_capabilities.h"
#include "json_escape.h"
#include "output_formatter.h"
#include "tools_system.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// External functions for OpenAI tool handling
extern char* generate_tools_json(const ToolRegistry *registry);
extern int parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count);
extern char* generate_single_tool_message(const ToolResult *result);
extern char* json_escape_string(const char *str);

// GPT models don't support thinking tags
static int gpt_process_response(const char* content, ParsedResponse* result) {
    if (!content || !result) {
        return -1;
    }
    
    // Initialize result
    result->thinking_content = NULL;
    result->response_content = NULL;
    
    // GPT models don't support thinking tags, entire content is the response
    size_t content_len = strlen(content);
    result->response_content = malloc(content_len + 1);
    if (!result->response_content) {
        return -1;
    }
    strcpy(result->response_content, content);
    
    return 0;
}

// GPT-specific tool message formatting for OpenAI-style APIs
static char* gpt_format_assistant_tool_message(const char* response_content, 
                                               const ToolCall* tool_calls, 
                                               int tool_call_count) {
    if (tool_call_count > 0 && tool_calls != NULL) {
        // For OpenAI, construct a message with tool_calls array manually
        // Estimate size needed
        size_t base_size = 200;
        size_t content_size = response_content ? strlen(response_content) * 2 + 50 : 50;
        
        // Calculate size for each tool call
        for (int i = 0; i < tool_call_count; i++) {
            base_size += 100; // Structure overhead
            base_size += strlen(tool_calls[i].id) * 2;
            base_size += strlen(tool_calls[i].name) * 2;
            base_size += strlen(tool_calls[i].arguments) * 2;
        }
        
        size_t total_size = base_size + content_size;
        char* message = malloc(total_size);
        if (!message) {
            return NULL;
        }
        
        char* current = message;
        size_t remaining = total_size;

        // Start the message structure with role field (required by OpenAI API)
        int written = snprintf(current, remaining, "{\"role\": \"assistant\", ");
        if (written < 0 || written >= (int)remaining) {
            free(message);
            return NULL;
        }
        current += written;
        remaining -= written;

        // Add content field (required by OpenAI API, can be null or empty string)
        if (response_content && strlen(response_content) > 0) {
            char* escaped_content = json_escape_string(response_content);
            if (!escaped_content) {
                free(message);
                return NULL;
            }

            written = snprintf(current, remaining, "\"content\": \"%s\", ", escaped_content);
            free(escaped_content);

            if (written < 0 || written >= (int)remaining) {
                free(message);
                return NULL;
            }
            current += written;
            remaining -= written;
        } else {
            // OpenAI requires content field even when null for tool call messages
            written = snprintf(current, remaining, "\"content\": null, ");
            if (written < 0 || written >= (int)remaining) {
                free(message);
                return NULL;
            }
            current += written;
            remaining -= written;
        }

        // Add tool_calls array
        written = snprintf(current, remaining, "\"tool_calls\": [");
        if (written < 0 || written >= (int)remaining) {
            free(message);
            return NULL;
        }
        current += written;
        remaining -= written;
        
        // Add each tool call
        for (int i = 0; i < tool_call_count; i++) {
            char* escaped_args = json_escape_string(tool_calls[i].arguments);
            if (!escaped_args) {
                free(message);
                return NULL;
            }
            
            written = snprintf(current, remaining,
                             "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                             i > 0 ? ", " : "",
                             tool_calls[i].id,
                             tool_calls[i].name,
                             escaped_args);
            
            free(escaped_args);
            
            if (written < 0 || written >= (int)remaining) {
                free(message);
                return NULL;
            }
            current += written;
            remaining -= written;
        }
        
        // Close the structure
        written = snprintf(current, remaining, "]}");
        if (written < 0 || written >= (int)remaining) {
            free(message);
            return NULL;
        }
        
        return message;
    } else if (response_content) {
        // No tool calls, just return the content wrapped in a message
        char* escaped_content = json_escape_string(response_content);
        if (!escaped_content) {
            return NULL;
        }
        
        size_t msg_size = strlen(escaped_content) + 50;
        char* message = malloc(msg_size);
        if (!message) {
            free(escaped_content);
            return NULL;
        }
        
        snprintf(message, msg_size, "{\"content\": \"%s\"}", escaped_content);
        free(escaped_content);
        
        return message;
    }
    
    return NULL;
}

// GPT model capabilities
static ModelCapabilities gpt_model = {
    .model_pattern = "gpt",
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = gpt_process_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = gpt_format_assistant_tool_message,
    .supports_structured_output = 1,
    .supports_json_mode = 1,
    .max_context_length = 128000
};

// O-series model capabilities (o1, o4, etc.) - same as GPT
static ModelCapabilities o_series_model = {
    .model_pattern = "o1",  // Matches o1-mini, o1-preview, etc.
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = gpt_process_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = gpt_format_assistant_tool_message,
    .supports_structured_output = 1,
    .supports_json_mode = 1,
    .max_context_length = 128000
};

// O4 model capabilities - same as GPT/O1
static ModelCapabilities o4_model = {
    .model_pattern = "o4",  // Matches o4-mini, o4-2025, etc.
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = gpt_process_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = gpt_format_assistant_tool_message,
    .supports_structured_output = 1,
    .supports_json_mode = 1,
    .max_context_length = 128000
};

int register_gpt_models(ModelRegistry* registry) {
    int result = register_model_capabilities(registry, &gpt_model);
    if (result != 0) return result;
    
    result = register_model_capabilities(registry, &o_series_model);
    if (result != 0) return result;
    
    return register_model_capabilities(registry, &o4_model);
}