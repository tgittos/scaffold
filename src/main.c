#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "http_client.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s \"<message>\"\n", argv[0]);
        fprintf(stderr, "Example: %s \"Hello, how are you?\"\n", argv[0]);
        return EXIT_FAILURE;
    }
    
    struct HTTPResponse response = {0};
    const char *url = "https://api.openai.com/v1/chat/completions";
    const char *user_message = argv[1];
    
    // Build JSON payload with user's message
    char post_data[2048];
    int json_ret = snprintf(post_data, sizeof(post_data),
        "{"
        "\"model\": \"gpt-3.5-turbo\","
        "\"messages\": ["
            "{"
                "\"role\": \"user\","
                "\"content\": \"%s\""
            "}"
        "],"
        "\"max_tokens\": 100"
        "}", user_message);
    
    if (json_ret < 0 || json_ret >= (int)sizeof(post_data)) {
        fprintf(stderr, "Error: Message too long\n");
        return EXIT_FAILURE;
    }
    
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key == NULL) {
        fprintf(stderr, "Error: OPENAI_API_KEY environment variable not set\n");
        return EXIT_FAILURE;
    }
    
    char auth_header[512];
    int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    if (ret < 0 || ret >= (int)sizeof(auth_header)) {
        fprintf(stderr, "Error: Authorization header too long\n");
        return EXIT_FAILURE;
    }
    
    const char *headers[] = {
        auth_header,
        NULL
    };
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    printf("Making OpenAI API request to %s\n", url);
    printf("POST data: %s\n\n", post_data);
    
    if (http_post_with_headers(url, post_data, headers, &response) == 0) {
        printf("OpenAI API Response:\n%s\n", response.data);
    } else {
        fprintf(stderr, "OpenAI API request failed\n");
        cleanup_response(&response);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
    
    cleanup_response(&response);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}