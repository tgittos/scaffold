#include "../llm_provider.h"
#include "../api_common.h"
#include "../json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int openai_detect_provider(const char* api_url);
static char* openai_build_request_json(const LLMProvider* provider,
                                      const char* model,
                                      const char* system_prompt,
                                      const ConversationHistory* conversation,
                                      const char* user_message,
                                      int max_tokens,
                                      const ToolRegistry* tools);
static int openai_build_headers(const LLMProvider* provider,
                               const char* api_key,
                               const char** headers,
                               int max_headers);
static int openai_parse_response(const LLMProvider* provider,
                                const char* json_response,
                                ParsedResponse* result);
static int openai_parse_tool_calls(const LLMProvider* provider,
                                  const char* json_response,
                                  ToolCall** tool_calls,
                                  int* call_count);
static char* openai_format_assistant_message(const LLMProvider* provider,
                                            const char* response_content,
                                            const ToolCall* tool_calls,
                                            int tool_call_count);

// OpenAI provider implementation
static int openai_detect_provider(const char* api_url) {
    return strstr(api_url, "api.openai.com") != NULL ||
           strstr(api_url, "openai.azure.com") != NULL ||  // Support Azure OpenAI
           strstr(api_url, "api.groq.com") != NULL;        // Support Groq (OpenAI-compatible)
}

static char* openai_build_request_json(const LLMProvider* provider,
                                      const char* model,
                                      const char* system_prompt,
                                      const ConversationHistory* conversation,
                                      const char* user_message,
                                      int max_tokens,
                                      const ToolRegistry* tools) {
    // OpenAI-specific request building - system prompt in messages array
    return build_json_payload_common(model, system_prompt, conversation,
                                   user_message, provider->capabilities.max_tokens_param,
                                   max_tokens, tools, format_openai_message, 0);
}

static int openai_build_headers(const LLMProvider* provider,
                               const char* api_key,
                               const char** headers,
                               int max_headers) {
    (void)provider; // Suppress unused parameter warning
    int count = 0;
    static char auth_header[512];
    static char content_type[] = "Content-Type: application/json";
    
    // Add authorization header if API key provided
    if (api_key && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[count++] = auth_header;
    }
    
    // Add content type header
    if (count < max_headers - 1) {
        headers[count++] = content_type;
    }
    
    return count;
}

static int openai_parse_response(const LLMProvider* provider,
                                const char* json_response,
                                ParsedResponse* result) {
    (void)provider; // Suppress unused parameter warning
    // Use existing OpenAI response parser
    return parse_api_response(json_response, result);
}

static int openai_parse_tool_calls(const LLMProvider* provider,
                                  const char* json_response,
                                  ToolCall** tool_calls,
                                  int* call_count) {
    (void)provider; // Suppress unused parameter warning
    // Use existing OpenAI tool call parser
    return parse_tool_calls(json_response, tool_calls, call_count);
}

static char* openai_format_assistant_message(const LLMProvider* provider,
                                            const char* response_content,
                                            const ToolCall* tool_calls,
                                            int tool_call_count) {
    (void)provider; // Suppress unused parameter warning
    
    if (tool_call_count > 0 && tool_calls != NULL) {
        // For OpenAI, construct a message with tool_calls array manually
        // Estimate size needed
        size_t base_size = 200;
        size_t content_size = response_content ? strlen(response_content) * 2 + 50 : 50;
        size_t tools_size = tool_call_count * 300;
        
        char* message = malloc(base_size + content_size + tools_size);
        if (message == NULL) {
            return NULL;
        }
        
        // Build the message manually
        char* escaped_content = json_escape_string(response_content ? response_content : "");
        if (escaped_content == NULL) {
            free(message);
            return NULL;
        }
        
        int written = snprintf(message, base_size + content_size + tools_size,
                              "{\"role\": \"assistant\", \"content\": \"%s\", \"tool_calls\": [",
                              escaped_content);
        free(escaped_content);
        
        if (written < 0) {
            free(message);
            return NULL;
        }
        
        // Add tool calls
        for (int i = 0; i < tool_call_count; i++) {
            char* escaped_args = json_escape_string(tool_calls[i].arguments ? tool_calls[i].arguments : "{}");
            if (escaped_args == NULL) {
                free(message);
                return NULL;
            }
            
            char tool_call_json[1024];
            int tool_written = snprintf(tool_call_json, sizeof(tool_call_json),
                                       "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                                       i > 0 ? ", " : "",
                                       tool_calls[i].id ? tool_calls[i].id : "",
                                       tool_calls[i].name ? tool_calls[i].name : "",
                                       escaped_args);
            free(escaped_args);
            
            if (tool_written < 0 || tool_written >= (int)sizeof(tool_call_json)) {
                free(message);
                return NULL;
            }
            
            strcat(message, tool_call_json);
        }
        
        strcat(message, "]}");
        return message;
    } else {
        // Simple message without tool calls
        return json_build_message("assistant", response_content ? response_content : "");
    }
}

// OpenAI provider instance
static LLMProvider openai_provider = {
    .capabilities = {
        .name = "OpenAI",
        .max_tokens_param = "max_completion_tokens",
        .supports_system_message = 1,
        .supports_tool_calling = 1,
        .requires_version_header = 0,
        .auth_header_format = "Authorization: Bearer %s",
        .version_header = NULL
    },
    .detect_provider = openai_detect_provider,
    .build_request_json = openai_build_request_json,
    .build_headers = openai_build_headers,
    .parse_response = openai_parse_response,
    .parse_tool_calls = openai_parse_tool_calls,
    .format_assistant_message = openai_format_assistant_message,
    .validate_conversation = NULL  // No special validation needed
};

int register_openai_provider(ProviderRegistry* registry) {
    return register_provider(registry, &openai_provider);
}