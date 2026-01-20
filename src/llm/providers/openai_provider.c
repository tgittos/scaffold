#include "llm_provider.h"
#include "api_common.h"
#include <cJSON.h>
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
// Tool calling functions removed - now handled by ModelCapabilities

// OpenAI provider implementation
static int openai_detect_provider(const char* api_url) {
    if (api_url == NULL) return 0;
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
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }
    // OpenAI-specific request building - system prompt in messages array
    return build_json_payload_model_aware(model, system_prompt, conversation,
                                        user_message, provider->capabilities.max_tokens_param,
                                        max_tokens, tools, format_openai_message, 0);
}

static int openai_build_headers(const LLMProvider* provider,
                               const char* api_key,
                               const char** headers,
                               int max_headers) {
    (void)provider; // Suppress unused parameter warning

    if (max_headers < 2) {
        return 0; // Cannot fit required headers
    }

    int count = 0;
    static _Thread_local char auth_header[512];
    static char content_type[] = "Content-Type: application/json";

    // Add authorization header if API key provided and non-empty
    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
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

// Tool calling implementation removed - now handled by ModelCapabilities

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
    // Tool functions removed - now handled by ModelCapabilities
    .validate_conversation = NULL  // No special validation needed
};

int register_openai_provider(ProviderRegistry* registry) {
    return register_provider(registry, &openai_provider);
}