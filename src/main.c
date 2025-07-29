#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "http_client.h"

int main(void)
{
    struct HTTPResponse response = {0};
    const char *url = "https://api.openai.com/v1/chat/completions";
    const char *post_data = "{"
        "\"model\": \"gpt-3.5-turbo\","
        "\"messages\": ["
            "{"
                "\"role\": \"user\","
                "\"content\": \"Hello from C! Please respond with a brief greeting.\""
            "}"
        "],"
        "\"max_tokens\": 100"
    "}";
    
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