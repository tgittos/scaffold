#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>
#include "http_client.h"

int main(void)
{
    struct HTTPResponse response = {0};
    const char *url = "https://httpbin.org/post";
    const char *post_data = "{\"message\": \"Hello from C!\", \"timestamp\": \"2024-01-01\"}";
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    printf("Making HTTP POST request to %s\n", url);
    printf("POST data: %s\n\n", post_data);
    
    if (http_post(url, post_data, &response) == 0) {
        printf("Response received:\n%s\n", response.data);
    } else {
        fprintf(stderr, "HTTP POST request failed\n");
        cleanup_response(&response);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
    
    cleanup_response(&response);
    curl_global_cleanup();
    return EXIT_SUCCESS;
}