#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <stddef.h>

struct HTTPResponse {
    char *data;
    size_t size;
};

struct HTTPConfig {
    long timeout_seconds;
    long connect_timeout_seconds;
    int follow_redirects;
    int max_redirects;
};

// Default configuration
extern const struct HTTPConfig DEFAULT_HTTP_CONFIG;

// =============================================================================
// Standard HTTP Functions (buffered responses)
// =============================================================================

int http_post(const char *url, const char *post_data, struct HTTPResponse *response);
int http_post_with_headers(const char *url, const char *post_data, const char **headers, struct HTTPResponse *response);
int http_post_with_config(const char *url, const char *post_data, const char **headers,
                         const struct HTTPConfig *config, struct HTTPResponse *response);

int http_get(const char *url, struct HTTPResponse *response);
int http_get_with_headers(const char *url, const char **headers, struct HTTPResponse *response);
int http_get_with_config(const char *url, const char **headers,
                        const struct HTTPConfig *config, struct HTTPResponse *response);

void cleanup_response(struct HTTPResponse *response);

// =============================================================================
// Streaming HTTP Functions
// =============================================================================

/**
 * Callback type for streaming HTTP responses
 *
 * @param data Chunk of response data
 * @param size Size of the data chunk
 * @param user_data User-provided context pointer
 * @return Number of bytes processed (should equal size on success)
 */
typedef size_t (*http_stream_callback_t)(const char *data, size_t size, void *user_data);

/**
 * Configuration for streaming HTTP requests
 *
 * Extends HTTPConfig with streaming-specific settings for handling
 * SSE (Server-Sent Events) and other streaming responses.
 */
struct StreamingHTTPConfig {
    struct HTTPConfig base;         // Base HTTP configuration
    http_stream_callback_t stream_callback;  // Called for each chunk
    void *callback_data;            // User data passed to callback
    long low_speed_limit;           // Bytes/sec threshold (default: 1)
    long low_speed_time;            // Seconds below threshold (default: 30)
};

// Default streaming configuration
extern const struct StreamingHTTPConfig DEFAULT_STREAMING_HTTP_CONFIG;

/**
 * Perform a streaming POST request
 *
 * Unlike http_post_with_config, this function calls the stream_callback
 * for each chunk of data received rather than buffering the entire response.
 * This is essential for SSE streams where data arrives incrementally.
 *
 * @param url The URL to POST to
 * @param post_data The POST body data
 * @param headers NULL-terminated array of headers
 * @param config Streaming configuration with callback
 * @return 0 on success, -1 on error
 */
int http_post_streaming(const char *url, const char *post_data,
                       const char **headers,
                       const struct StreamingHTTPConfig *config);

#endif /* HTTP_CLIENT_H */