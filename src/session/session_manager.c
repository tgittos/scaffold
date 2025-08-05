#include "session_manager.h"
#include "debug_output.h"
#include "http_client.h"
#include "json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void session_data_init(SessionData* session) {
    if (session == NULL) return;
    
    memset(session, 0, sizeof(SessionData));
    init_conversation_history(&session->conversation);
}

void session_data_cleanup(SessionData* session) {
    if (session == NULL) return;
    
    free(session->config.api_url);
    free(session->config.model);
    free(session->config.api_key);
    free(session->config.system_prompt);
    cleanup_conversation_history(&session->conversation);
    
    memset(session, 0, sizeof(SessionData));
}

void session_data_copy_config(SessionData* dest, const SessionConfig* src) {
    if (dest == NULL || src == NULL) return;
    
    // Free existing config
    free(dest->config.api_url);
    free(dest->config.model);
    free(dest->config.api_key);
    free(dest->config.system_prompt);
    
    // Copy new config
    dest->config.api_url = src->api_url ? strdup(src->api_url) : NULL;
    dest->config.model = src->model ? strdup(src->model) : NULL;
    dest->config.api_key = src->api_key ? strdup(src->api_key) : NULL;
    dest->config.system_prompt = src->system_prompt ? strdup(src->system_prompt) : NULL;
    dest->config.context_window = src->context_window;
    dest->config.max_context_window = src->max_context_window;
    dest->config.api_type = src->api_type;
}

char* session_build_api_payload(const SessionData* session, const char* user_message, 
                                int max_tokens, int include_tools) {
    if (session == NULL || user_message == NULL) return NULL;
    
    // For now, use a simple implementation focused on summarization
    // This is used primarily by the conversation compactor
    (void)include_tools; // Suppress unused parameter warning
    
    // Calculate buffer size needed
    size_t buffer_size = 2048; // Base size
    if (session->config.system_prompt) {
        buffer_size += strlen(session->config.system_prompt) * 2; // Account for escaping
    }
    buffer_size += strlen(user_message) * 2; // Account for escaping
    
    char* payload = malloc(buffer_size);
    if (payload == NULL) return NULL;
    
    // Build simple JSON payload
    char* escaped_system = session->config.system_prompt ? json_escape_string(session->config.system_prompt) : NULL;
    char* escaped_user = json_escape_string(user_message);
    
    if (escaped_user == NULL) {
        free(escaped_system);
        free(payload);
        return NULL;
    }
    
    const char* max_tokens_param = (session->config.api_type == 1) ? "max_tokens" : "max_completion_tokens";
    
    if (escaped_system) {
        snprintf(payload, buffer_size,
            "{"
            "\"model\": \"%s\","
            "\"%s\": %d,"
            "\"messages\": ["
            "{\"role\": \"system\", \"content\": \"%s\"},"
            "{\"role\": \"user\", \"content\": \"%s\"}"
            "]"
            "}",
            session->config.model ? session->config.model : "gpt-4",
            max_tokens_param, max_tokens,
            escaped_system, escaped_user);
    } else {
        snprintf(payload, buffer_size,
            "{"
            "\"model\": \"%s\","
            "\"%s\": %d,"
            "\"messages\": ["
            "{\"role\": \"user\", \"content\": \"%s\"}"
            "]"
            "}",
            session->config.model ? session->config.model : "gpt-4",
            max_tokens_param, max_tokens,
            escaped_user);
    }
    
    free(escaped_system);
    free(escaped_user);
    return payload;
}

int session_make_api_request(const SessionData* session, const char* payload, 
                            char** response_content) {
    if (session == NULL || payload == NULL || response_content == NULL) {
        return -1;
    }
    
    *response_content = NULL;
    
    // Setup headers
    char auth_header[512] = {0};
    char anthropic_version[128] = "anthropic-version: 2023-06-01";
    char content_type[64] = "Content-Type: application/json";
    const char *headers[4] = {NULL, NULL, NULL, NULL};
    int header_count = 0;
    
    if (session->config.api_key != NULL) {
        if (session->config.api_type == 1) {  // Anthropic
            snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", session->config.api_key);
            headers[header_count++] = auth_header;
            headers[header_count++] = anthropic_version;
        } else {  // OpenAI/Local
            snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", session->config.api_key);
            headers[header_count++] = auth_header;
        }
    }
    headers[header_count++] = content_type;
    
    // Make HTTP request
    struct HTTPResponse response = {0};
    debug_printf("Making API request to %s\n", session->config.api_url);
    
    if (http_post_with_headers(session->config.api_url, payload, headers, &response) != 0) {
        debug_printf("Error: HTTP request failed\n");
        return -1;
    }
    
    if (response.data == NULL) {
        debug_printf("Error: No response data from API\n");
        cleanup_response(&response);
        return -1;
    }
    
    // Extract content based on API type
    char* extracted_content = NULL;
    
    if (session->config.api_type == 1) {  // Anthropic
        // Look for {"content":[{"text":"response here"}]}
        const char* content_start = strstr(response.data, "\"content\"");
        if (content_start != NULL) {
            const char* text_start = strstr(content_start, "\"text\":\"");
            if (text_start != NULL) {
                text_start += 8; // Skip "text":"
                const char* text_end = strstr(text_start, "\"}");
                if (text_end != NULL) {
                    size_t content_len = text_end - text_start;
                    extracted_content = malloc(content_len + 1);
                    if (extracted_content != NULL) {
                        strncpy(extracted_content, text_start, content_len);
                        extracted_content[content_len] = '\0';
                    }
                }
            }
        }
    } else {  // OpenAI/Local
        // Look for {"choices":[{"message":{"content":"response here"}}]}
        const char* content_start = strstr(response.data, "\"content\":\"");
        if (content_start != NULL) {
            content_start += 11; // Skip "content":"
            const char* content_end = strstr(content_start, "\"");
            if (content_end != NULL) {
                size_t content_len = content_end - content_start;
                extracted_content = malloc(content_len + 1);
                if (extracted_content != NULL) {
                    strncpy(extracted_content, content_start, content_len);
                    extracted_content[content_len] = '\0';
                }
            }
        }
    }
    
    cleanup_response(&response);
    
    if (extracted_content == NULL) {
        debug_printf("Error: Failed to extract content from API response\n");
        return -1;
    }
    
    *response_content = extracted_content;
    return 0;
}