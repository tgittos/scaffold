#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

struct HTTPResponse {
    char *data;
    size_t size;
    long http_status;
    char *content_type;
};

struct HTTPConfig {
    long timeout_seconds;
    long connect_timeout_seconds;
    int follow_redirects;
    int max_redirects;
};

extern const struct HTTPConfig DEFAULT_HTTP_CONFIG;

// =============================================================================
// Standard HTTP Functions (buffered responses)
// =============================================================================

int http_post_with_headers(const char *url, const char *post_data, const char **headers, struct HTTPResponse *response);
int http_post_with_config(const char *url, const char *post_data, const char **headers,
                         const struct HTTPConfig *config, struct HTTPResponse *response);

int http_get(const char *url, struct HTTPResponse *response);
int http_get_with_config(const char *url, const char **headers,
                        const struct HTTPConfig *config, struct HTTPResponse *response);

size_t http_write_callback(void *contents, size_t size, size_t nmemb, void *userp);
void cleanup_response(struct HTTPResponse *response);
void http_configure_ssl(void *curl_handle);

// =============================================================================
// File Download Functions
// =============================================================================

int http_download_file(const char *url, const char **headers,
                       const struct HTTPConfig *config,
                       const char *dest_path, size_t *bytes_written);

// =============================================================================
// Streaming HTTP Functions
// =============================================================================

// Must return size on success; returning less signals an error to curl
typedef size_t (*http_stream_callback_t)(const char *data, size_t size, void *user_data);

struct StreamingHTTPConfig {
    struct HTTPConfig base;
    http_stream_callback_t stream_callback;
    void *callback_data;
    long low_speed_limit;  // Bytes/sec; curl aborts if below this for low_speed_time
    long low_speed_time;   // Seconds
};

extern const struct StreamingHTTPConfig DEFAULT_STREAMING_HTTP_CONFIG;

int http_post_streaming(const char *url, const char *post_data,
                       const char **headers,
                       const struct StreamingHTTPConfig *config);

#endif /* HTTP_CLIENT_H */
