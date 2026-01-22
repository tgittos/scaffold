#include "llm_provider.h"
#include "api_common.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int anthropic_detect_provider(const char* api_url);
static char* anthropic_build_request_json(const LLMProvider* provider,
                                         const char* model,
                                         const char* system_prompt,
                                         const ConversationHistory* conversation,
                                         const char* user_message,
                                         int max_tokens,
                                         const ToolRegistry* tools);
static int anthropic_build_headers(const LLMProvider* provider,
                                  const char* api_key,
                                  const char** headers,
                                  int max_headers);
static int anthropic_parse_response(const LLMProvider* provider,
                                   const char* json_response,
                                   ParsedResponse* result);
// Tool calling functions removed - now handled by ModelCapabilities
static int anthropic_validate_conversation(const LLMProvider* provider,
                                          const ConversationHistory* conversation);

// Helper function to check if a tool_use_id exists in an assistant message
static int message_contains_tool_use_id(const ConversationMessage* msg, const char* tool_use_id) {
    if (!msg || !tool_use_id || strcmp(msg->role, "assistant") != 0) {
        return 0;
    }
    
    // Search for the tool_use_id in the message content
    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "\"id\": \"%s\"", tool_use_id);
    
    if (strstr(msg->content, search_pattern)) {
        return 1;
    }
    
    // Also try without spaces around colon
    snprintf(search_pattern, sizeof(search_pattern), "\"id\":\"%s\"", tool_use_id);
    return strstr(msg->content, search_pattern) != NULL;
}

// Anthropic provider implementation
static int anthropic_detect_provider(const char* api_url) {
    if (api_url == NULL) return 0;
    return strstr(api_url, "api.anthropic.com") != NULL;
}

static char* anthropic_build_request_json(const LLMProvider* provider,
                                         const char* model,
                                         const char* system_prompt,
                                         const ConversationHistory* conversation,
                                         const char* user_message,
                                         int max_tokens,
                                         const ToolRegistry* tools) {
    if (provider == NULL || model == NULL || conversation == NULL) {
        return NULL;
    }
    // Anthropic-specific request building - system prompt at top level
    // Use the specialized Anthropic message builder to handle tool_result validation
    return build_json_payload_model_aware(model, system_prompt, conversation,
                                        user_message, provider->capabilities.max_tokens_param,
                                        max_tokens, tools, format_anthropic_message, 1);
}

static int anthropic_build_headers(const LLMProvider* provider,
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
    static char version_header[] = "anthropic-version: 2023-06-01";

    // Add x-api-key header if API key provided
    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
        headers[count++] = auth_header;
    }

    // Add version header (required by Anthropic)
    if (count < max_headers - 1) {
        headers[count++] = version_header;
    }

    // Add content type header
    if (count < max_headers - 1) {
        headers[count++] = content_type;
    }

    return count;
}

static int anthropic_parse_response(const LLMProvider* provider,
                                   const char* json_response,
                                   ParsedResponse* result) {
    (void)provider; // Suppress unused parameter warning
    // Use existing Anthropic response parser
    return parse_anthropic_response(json_response, result);
}

// Tool calling implementation removed - now handled by ModelCapabilities

static int anthropic_validate_conversation(const LLMProvider* provider,
                                          const ConversationHistory* conversation) {
    (void)provider; // Suppress unused parameter warning
    // Anthropic-specific validation: tool_result must have corresponding tool_use
    // This logic was moved here from api_common.c
    
    for (int i = 0; i < conversation->count; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        
        // For tool results, verify the previous assistant message contains the tool_use_id
        if (strcmp(msg->role, "tool") == 0 && msg->tool_call_id != NULL) {
            // Look backwards for the most recent assistant message
            int found_tool_use = 0;
            
            for (int j = i - 1; j >= 0; j--) {
                const ConversationMessage* prev_msg = &conversation->messages[j];
                
                if (strcmp(prev_msg->role, "assistant") == 0) {
                    if (message_contains_tool_use_id(prev_msg, msg->tool_call_id)) {
                        found_tool_use = 1;
                    }
                    break; // Stop at first assistant message found
                }
                
                // If we hit another role that breaks the sequence, stop looking
                if (strcmp(prev_msg->role, "user") == 0) {
                    break;
                }
            }
            
            // Report orphaned tool results (but don't fail - they'll be filtered)
            if (!found_tool_use) {
                fprintf(stderr, "Warning: Orphaned tool result with ID %s will be filtered\n", 
                       msg->tool_call_id);
            }
        }
    }
    
    return 0; // Always succeed - orphaned results are handled by message builder
}

// Streaming support - not yet implemented for Anthropic
static int anthropic_supports_streaming(const LLMProvider* provider) {
    (void)provider;
    return 0;  // Anthropic streaming not yet implemented
}

// Anthropic provider instance
static LLMProvider anthropic_provider = {
    .capabilities = {
        .name = "Anthropic",
        .max_tokens_param = "max_tokens",
        .supports_system_message = 1,
        .supports_tool_calling = 1,
        .requires_version_header = 1,
        .auth_header_format = "x-api-key: %s",
        .version_header = "anthropic-version: 2023-06-01"
    },
    .detect_provider = anthropic_detect_provider,
    .build_request_json = anthropic_build_request_json,
    .build_headers = anthropic_build_headers,
    .parse_response = anthropic_parse_response,
    // Tool functions removed - now handled by ModelCapabilities
    .validate_conversation = anthropic_validate_conversation,
    // Streaming not yet implemented for Anthropic
    .supports_streaming = anthropic_supports_streaming,
    .parse_stream_event = NULL,
    .build_streaming_request_json = NULL
};

int register_anthropic_provider(ProviderRegistry* registry) {
    return register_provider(registry, &anthropic_provider);
}