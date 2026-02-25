#include "llm_client.h"
#include "llm_provider.h"
#include "../network/http_client.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static llm_credential_provider_fn g_credential_fn = NULL;
static void *g_credential_data = NULL;

void llm_client_set_credential_provider(llm_credential_provider_fn fn,
                                         void *user_data) {
    g_credential_fn = fn;
    g_credential_data = user_data;
}

int llm_client_refresh_credential(char *key_buf, size_t key_buf_len) {
    if (g_credential_fn) {
        return g_credential_fn(key_buf, key_buf_len, g_credential_data);
    }
    return -1;
}

int llm_client_init(void) {
    return 0;
}

void llm_client_cleanup(void) {
}

int llm_client_send(const char* api_url, const char** headers,
                    const char* payload, struct HTTPResponse* response) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    int result = http_post_with_headers(api_url, payload, headers, response);
    curl_global_cleanup();
    return result;
}

int llm_client_send_streaming(const char* api_url, const char** headers,
                              const char* payload,
                              struct StreamingHTTPConfig* config) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    int result = http_post_streaming(api_url, payload, headers, config);
    curl_global_cleanup();
    return result;
}
