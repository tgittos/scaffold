#include "llm_client.h"
#include "llm_provider.h"
#include "../network/http_client.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

int llm_client_init(void) {
    return curl_global_init(CURL_GLOBAL_DEFAULT);
}

void llm_client_cleanup(void) {
    curl_global_cleanup();
}

static int build_headers_for_url(const char* api_url, const char* api_key,
                                 const char** headers, int max_headers) {
    ProviderRegistry* registry = get_provider_registry();
    if (registry == NULL) {
        return 0;
    }

    LLMProvider* provider = detect_provider_for_url(registry, api_url);
    if (provider == NULL || provider->build_headers == NULL) {
        return 0;
    }

    return provider->build_headers(provider, api_key, headers, max_headers);
}

int llm_client_send(const char* api_url, const char* api_key,
                    const char* payload, struct HTTPResponse* response) {
    const char* headers[8] = {0};
    build_headers_for_url(api_url, api_key, headers, 8);
    return http_post_with_headers(api_url, payload, headers, response);
}

int llm_client_send_streaming(const char* api_url, const char* api_key,
                              const char* payload,
                              struct StreamingHTTPConfig* config) {
    const char* headers[8] = {0};
    build_headers_for_url(api_url, api_key, headers, 8);
    return http_post_streaming(api_url, payload, headers, config);
}
