#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "http_client.h"
#include "env_loader.h"

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
    
    // Get API URL from environment variable, default to OpenAI
    const char *url = getenv("API_URL");
    if (url == NULL) {
        url = "https://api.openai.com/v1/chat/completions";
    }
    
    // Get model from environment variable, default to gpt-3.5-turbo
    const char *model = getenv("MODEL");
    if (model == NULL) {
        model = "gpt-3.5-turbo";
    }
    
    // Build JSON payload with user's message
    char post_data[2048];
    int json_ret = snprintf(post_data, sizeof(post_data),
        "{"
        "\"model\": \"%s\","
        "\"messages\": ["
            "{"
                "\"role\": \"user\","
                "\"content\": \"%s\""
            "}"
        "],"
        "\"max_tokens\": 100"
        "}", model, user_message);
    
    if (json_ret < 0 || json_ret >= (int)sizeof(post_data)) {
        fprintf(stderr, "Error: Message too long\n");
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
            return EXIT_FAILURE;
        }
        headers[0] = auth_header;
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    fprintf(stderr, "Making API request to %s\n", url);
    fprintf(stderr, "POST data: %s\n\n", post_data);
    
    if (http_post_with_headers(url, post_data, headers, &response) == 0) {
        printf("%s\n", response.data);
    } else {
        fprintf(stderr, "API request failed\n");
        cleanup_response(&response);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
    
    cleanup_response(&response);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}