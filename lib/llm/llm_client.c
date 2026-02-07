#include "llm_client.h"
#include "llm_provider.h"
#include "../network/http_client.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

int llm_client_init(void) {
    return 0;
}

void llm_client_cleanup(void) {
}

int llm_client_send(const char* api_url, const char* api_key,
                    const char* payload, struct HTTPResponse* response) {
    char auth_header[512];
    const char* headers[4] = {0};
    if (api_key && strlen(api_key) > 0) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[0] = auth_header;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    int result = http_post_with_headers(api_url, payload, headers, response);
    curl_global_cleanup();
    return result;
}

int llm_client_send_streaming(const char* api_url, const char* api_key,
                              const char* payload,
                              struct StreamingHTTPConfig* config) {
    char auth_header[512];
    const char* headers[4] = {0};
    if (api_key && strlen(api_key) > 0) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[0] = auth_header;
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    int result = http_post_streaming(api_url, payload, headers, config);
    curl_global_cleanup();
    return result;
}
