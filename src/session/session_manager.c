#include "session_manager.h"
#include "debug_output.h"
#include "http_client.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_escape.h"
#include <stdint.h>

// Buffer size calculation constants
#define PAYLOAD_BASE_SIZE 2048
#define JSON_ESCAPE_MULTIPLIER 6  // Worst case: all chars need \uXXXX escaping
#define MAX_PAYLOAD_SIZE (SIZE_MAX / 2)  // Prevent overflow in calculations

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

int session_data_copy_config(SessionData* dest, const SessionConfig* src) {
    if (dest == NULL || src == NULL) return -1;

    // Stage allocations to avoid partial state on failure
    char *new_api_url = NULL;
    char *new_model = NULL;
    char *new_api_key = NULL;
    char *new_system_prompt = NULL;

    if (src->api_url) {
        new_api_url = strdup(src->api_url);
        if (new_api_url == NULL) goto alloc_failed;
    }
    if (src->model) {
        new_model = strdup(src->model);
        if (new_model == NULL) goto alloc_failed;
    }
    if (src->api_key) {
        new_api_key = strdup(src->api_key);
        if (new_api_key == NULL) goto alloc_failed;
    }
    if (src->system_prompt) {
        new_system_prompt = strdup(src->system_prompt);
        if (new_system_prompt == NULL) goto alloc_failed;
    }

    // All allocations succeeded - commit the changes
    free(dest->config.api_url);
    free(dest->config.model);
    free(dest->config.api_key);
    free(dest->config.system_prompt);

    dest->config.api_url = new_api_url;
    dest->config.model = new_model;
    dest->config.api_key = new_api_key;
    dest->config.system_prompt = new_system_prompt;
    dest->config.context_window = src->context_window;
    dest->config.api_type = src->api_type;

    return 0;

alloc_failed:
    free(new_api_url);
    free(new_model);
    free(new_api_key);
    free(new_system_prompt);
    return -1;
}

char* session_build_api_payload(const SessionData* session, const char* user_message, 
                                int max_tokens, int include_tools) {
    if (session == NULL || user_message == NULL) return NULL;
    
    // For now, use a simple implementation focused on summarization
    // This is used primarily by the conversation compactor
    (void)include_tools; // Suppress unused parameter warning
    
    // Calculate buffer size needed with overflow protection
    size_t system_len = session->config.system_prompt ? strlen(session->config.system_prompt) : 0;
    size_t user_len = strlen(user_message);

    // Check for potential overflow before multiplication
    if (system_len > MAX_PAYLOAD_SIZE / JSON_ESCAPE_MULTIPLIER ||
        user_len > MAX_PAYLOAD_SIZE / JSON_ESCAPE_MULTIPLIER) {
        return NULL;  // Input too large, would overflow
    }

    size_t system_escaped_size = system_len * JSON_ESCAPE_MULTIPLIER;
    size_t user_escaped_size = user_len * JSON_ESCAPE_MULTIPLIER;

    // Check for overflow in addition
    if (PAYLOAD_BASE_SIZE > MAX_PAYLOAD_SIZE - system_escaped_size - user_escaped_size) {
        return NULL;  // Total size would overflow
    }

    size_t buffer_size = PAYLOAD_BASE_SIZE + system_escaped_size + user_escaped_size;

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
    
    // Extract content based on API type using proper JSON parsing
    char* extracted_content = NULL;
    cJSON* root = cJSON_Parse(response.data);

    if (root != NULL) {
        if (session->config.api_type == 1) {  // Anthropic
            // Format: {"content":[{"type":"text","text":"response here"}]}
            cJSON* content_array = cJSON_GetObjectItem(root, "content");
            if (content_array != NULL && cJSON_IsArray(content_array)) {
                cJSON* first_content = cJSON_GetArrayItem(content_array, 0);
                if (first_content != NULL) {
                    cJSON* text_item = cJSON_GetObjectItem(first_content, "text");
                    if (text_item != NULL && cJSON_IsString(text_item)) {
                        const char* text_value = cJSON_GetStringValue(text_item);
                        if (text_value != NULL) {
                            extracted_content = strdup(text_value);
                        }
                    }
                }
            }
        } else {  // OpenAI/Local
            // Format: {"choices":[{"message":{"content":"response here"}}]}
            cJSON* choices = cJSON_GetObjectItem(root, "choices");
            if (choices != NULL && cJSON_IsArray(choices)) {
                cJSON* first_choice = cJSON_GetArrayItem(choices, 0);
                if (first_choice != NULL) {
                    cJSON* message = cJSON_GetObjectItem(first_choice, "message");
                    if (message != NULL) {
                        cJSON* content_item = cJSON_GetObjectItem(message, "content");
                        if (content_item != NULL && cJSON_IsString(content_item)) {
                            const char* content_value = cJSON_GetStringValue(content_item);
                            if (content_value != NULL) {
                                extracted_content = strdup(content_value);
                            }
                        }
                    }
                }
            }
        }
        cJSON_Delete(root);
    }

    cleanup_response(&response);
    
    if (extracted_content == NULL) {
        debug_printf("Error: Failed to extract content from API response\n");
        return -1;
    }
    
    *response_content = extracted_content;
    return 0;
}