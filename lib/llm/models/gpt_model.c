#include "../model_capabilities.h"
#include "../../util/json_escape.h"
#include "../../tools/tools_system.h"
#include "response_processing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Buffer size constants for tool message formatting
#define GPT_TOOL_MSG_BASE_SIZE 200
#define GPT_TOOL_CALL_OVERHEAD 100
// JSON escape can expand a single character up to 6 characters (e.g., \u0000)
#define JSON_ESCAPE_MULTIPLIER 6
#define GPT_CONTENT_PADDING 50

// GPT-specific tool message formatting for OpenAI-style APIs
static char* gpt_format_assistant_tool_message(const char* response_content,
                                               const ToolCall* tool_calls,
                                               int tool_call_count) {
    if (tool_call_count > 0 && tool_calls != NULL) {
        // For OpenAI, construct a message with tool_calls array manually
        // Estimate size needed
        size_t base_size = GPT_TOOL_MSG_BASE_SIZE;
        size_t content_size = response_content ?
            strlen(response_content) * JSON_ESCAPE_MULTIPLIER + GPT_CONTENT_PADDING : GPT_CONTENT_PADDING;

        // Calculate size for each tool call with NULL checks
        for (int i = 0; i < tool_call_count; i++) {
            base_size += GPT_TOOL_CALL_OVERHEAD;
            base_size += tool_calls[i].id ? strlen(tool_calls[i].id) * JSON_ESCAPE_MULTIPLIER : 0;
            base_size += tool_calls[i].name ? strlen(tool_calls[i].name) * JSON_ESCAPE_MULTIPLIER : 0;
            base_size += tool_calls[i].arguments ? strlen(tool_calls[i].arguments) * JSON_ESCAPE_MULTIPLIER : 0;
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

        // Add each tool call with NULL checks
        for (int i = 0; i < tool_call_count; i++) {
            const char* id = tool_calls[i].id ? tool_calls[i].id : "";
            const char* name = tool_calls[i].name ? tool_calls[i].name : "";
            const char* args = tool_calls[i].arguments ? tool_calls[i].arguments : "";

            char* escaped_args = json_escape_string(args);
            if (!escaped_args) {
                free(message);
                return NULL;
            }

            written = snprintf(current, remaining,
                             "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                             i > 0 ? ", " : "",
                             id,
                             name,
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

        size_t msg_size = strlen(escaped_content) + GPT_CONTENT_PADDING;
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
    .process_response = process_simple_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = gpt_format_assistant_tool_message,
    .max_context_length = 128000
};

// O-series model capabilities (o1, o4, etc.) - same as GPT
static ModelCapabilities o_series_model = {
    .model_pattern = "o1",
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = process_simple_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = gpt_format_assistant_tool_message,
    .max_context_length = 128000
};

// O4 model capabilities - same as GPT/O1
static ModelCapabilities o4_model = {
    .model_pattern = "o4",
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = process_simple_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = gpt_format_assistant_tool_message,
    .max_context_length = 128000
};

int register_gpt_models(ModelRegistry* registry) {
    int result = register_model_capabilities(registry, &gpt_model);
    if (result != 0) return result;

    result = register_model_capabilities(registry, &o_series_model);
    if (result != 0) return result;

    return register_model_capabilities(registry, &o4_model);
}
