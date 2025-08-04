#include "../llm_provider.h"
#include "../api_common.h"
#include "../json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int local_ai_detect_provider(const char* api_url);
static char* local_ai_build_request_json(const LLMProvider* provider,
                                        const char* model,
                                        const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message,
                                        int max_tokens,
                                        const ToolRegistry* tools);
static int local_ai_build_headers(const LLMProvider* provider,
                                 const char* api_key,
                                 const char** headers,
                                 int max_headers);
static int local_ai_parse_response(const LLMProvider* provider,
                                  const char* json_response,
                                  ParsedResponse* result);
// Tool calling functions removed - now handled by ModelCapabilities

// Local AI provider implementation
static int local_ai_detect_provider(const char* api_url) {
    // Local AI is the fallback provider - anything that's not Anthropic or OpenAI
    // This should be checked LAST in the provider registry
    if (api_url == NULL) return 0;
    
    // Explicitly exclude known cloud providers
    if (strstr(api_url, "api.anthropic.com") != NULL ||
        strstr(api_url, "api.openai.com") != NULL ||
        strstr(api_url, "openai.azure.com") != NULL ||
        strstr(api_url, "api.groq.com") != NULL) {
        return 0;
    }
    
    // Everything else is considered local AI (including remote LM servers)
    return 1;
}

static char* local_ai_build_request_json(const LLMProvider* provider,
                                        const char* model,
                                        const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message,
                                        int max_tokens,
                                        const ToolRegistry* tools) {
    // Local AI typically follows OpenAI format - system prompt in messages array
    return build_json_payload_model_aware(model, system_prompt, conversation,
                                        user_message, provider->capabilities.max_tokens_param,
                                        max_tokens, tools, format_openai_message, 0);
}

static int local_ai_build_headers(const LLMProvider* provider,
                                 const char* api_key,
                                 const char** headers,
                                 int max_headers) {
    (void)provider; // Suppress unused parameter warning
    int count = 0;
    static char auth_header[512];
    static char content_type[] = "Content-Type: application/json";
    
    // Add authorization header if API key provided (some local servers require it)
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

static int local_ai_parse_response(const LLMProvider* provider,
                                  const char* json_response,
                                  ParsedResponse* result) {
    (void)provider; // Suppress unused parameter warning
    // Local AI typically follows OpenAI response format
    return parse_api_response(json_response, result);
}

// Tool calling implementation removed - now handled by ModelCapabilities

// Local AI provider instance
static LLMProvider local_ai_provider = {
    .capabilities = {
        .name = "Local AI",
        .max_tokens_param = "max_tokens",
        .supports_system_message = 1,
        .supports_tool_calling = 1,  // Many local servers support this now
        .requires_version_header = 0,
        .auth_header_format = "Authorization: Bearer %s",
        .version_header = NULL
    },
    .detect_provider = local_ai_detect_provider,
    .build_request_json = local_ai_build_request_json,
    .build_headers = local_ai_build_headers,
    .parse_response = local_ai_parse_response,
    // Tool functions removed - now handled by ModelCapabilities
    .validate_conversation = NULL  // No special validation needed
};

int register_local_ai_provider(ProviderRegistry* registry) {
    return register_provider(registry, &local_ai_provider);
}