#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "http_client.h"
#include "env_loader.h"
#include "output_formatter.h"
#include "prompt_loader.h"

int main(int argc, char *argv[])
{
    // Load environment variables from .env file
    if (load_env_file(".env") != 0) {
        fprintf(stderr, "Error: Failed to load .env file\n");
        return EXIT_FAILURE;
    }
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s \"<message>\"\n", argv[0]);
        fprintf(stderr, "Example: %s \"Hello, how are you?\"\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    struct HTTPResponse response = {0};
    const char *user_message = argv[1];
    char *system_prompt = NULL;
    
    // Try to load system prompt from PROMPT.md (optional)
    load_system_prompt(&system_prompt);
    
    // Get API URL from environment variable, default to OpenAI
    const char *url = getenv("API_URL");
    if (url == NULL) {
        url = "https://api.openai.com/v1/chat/completions";
    }
    
    // Get model from environment variable, default to gpt-3.5-turbo
    const char *model = getenv("MODEL");
    if (model == NULL) {
        model = "o4-mini-2025-04-16";
    }
    
    // Get context window size from environment variable
    const char *context_window_str = getenv("CONTEXT_WINDOW");
    int context_window = 8192;  // Default for many models
    if (context_window_str != NULL) {
        int parsed_window = atoi(context_window_str);
        if (parsed_window > 0) {
            context_window = parsed_window;
        }
    }
    
    // Get max response tokens from environment variable (optional override)
    const char *max_tokens_str = getenv("MAX_TOKENS");
    int max_tokens = -1;  // Will be calculated later if not set
    if (max_tokens_str != NULL) {
        int parsed_tokens = atoi(max_tokens_str);
        if (parsed_tokens > 0) {
            max_tokens = parsed_tokens;
        }
    }
    
    // Estimate prompt tokens (rough approximation: ~4 chars per token)
    int estimated_prompt_tokens = (int)(strlen(user_message) / 4) + 20; // +20 for system overhead
    
    // Calculate max response tokens if not explicitly set
    if (max_tokens == -1) {
        max_tokens = context_window - estimated_prompt_tokens - 50; // -50 for safety buffer
        if (max_tokens < 100) {
            max_tokens = 100; // Minimum reasonable response length
        }
    }
    
    // Determine the correct parameter name for max tokens
    // OpenAI uses "max_completion_tokens" for newer models, while local servers typically use "max_tokens"
    const char *max_tokens_param = "max_tokens";
    if (strstr(url, "api.openai.com") != NULL) {
        max_tokens_param = "max_completion_tokens";
    }
    
    // Build JSON payload with system prompt (if available) and user's message
    char post_data[4096];  // Increased size to accommodate system prompt
    int json_ret;
    
    if (system_prompt != NULL) {
        // Include system prompt
        json_ret = snprintf(post_data, sizeof(post_data),
            "{"
            "\"model\": \"%s\","
            "\"messages\": ["
                "{"
                    "\"role\": \"system\","
                    "\"content\": \"%s\""
                "},"
                "{"
                    "\"role\": \"user\","
                    "\"content\": \"%s\""
                "}"
            "],"
            "\"%s\": %d"
            "}", model, system_prompt, user_message, max_tokens_param, max_tokens);
    } else {
        // No system prompt, just user message
        json_ret = snprintf(post_data, sizeof(post_data),
            "{"
            "\"model\": \"%s\","
            "\"messages\": ["
                "{"
                    "\"role\": \"user\","
                    "\"content\": \"%s\""
                "}"
            "],"
            "\"%s\": %d"
            "}", model, user_message, max_tokens_param, max_tokens);
    }
    
    if (json_ret < 0 || json_ret >= (int)sizeof(post_data)) {
        fprintf(stderr, "Error: Message too long\n");
        cleanup_system_prompt(&system_prompt);
        return EXIT_FAILURE;
    }
    
    // Setup authorization - optional for local servers
    const char *api_key = getenv("OPENAI_API_KEY");
    char auth_header[512];
    const char *headers[2] = {NULL, NULL};
    
    if (api_key != NULL) {
        int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        if (ret < 0 || ret >= (int)sizeof(auth_header)) {
            fprintf(stderr, "Error: Authorization header too long\n");
            cleanup_system_prompt(&system_prompt);
            return EXIT_FAILURE;
        }
        headers[0] = auth_header;
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    fprintf(stderr, "Making API request to %s\n", url);
    fprintf(stderr, "POST data: %s\n\n", post_data);
    
    if (http_post_with_headers(url, post_data, headers, &response) == 0) {
        ParsedResponse parsed_response;
        if (parse_api_response(response.data, &parsed_response) == 0) {
            print_formatted_response(&parsed_response);
            cleanup_parsed_response(&parsed_response);
        } else {
            fprintf(stderr, "Error: Failed to parse API response\n");
            printf("%s\n", response.data);  // Fallback to raw output
        }
    } else {
        fprintf(stderr, "API request failed\n");
        cleanup_response(&response);
        cleanup_system_prompt(&system_prompt);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
    
    cleanup_response(&response);
    cleanup_system_prompt(&system_prompt);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}