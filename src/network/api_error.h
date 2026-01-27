#ifndef API_ERROR_H
#define API_ERROR_H

#include <curl/curl.h>

typedef struct {
    int is_retryable;
    int attempts_made;
    long http_status;           // 0 if network error (no HTTP response)
    CURLcode curl_code;
    char error_message[256];
} APIError;

void api_error_init(APIError *err);
void api_error_set(APIError *err, long http_status, CURLcode curl_code, int attempts);

// Returns a static user-friendly string suitable for display
const char* api_error_user_message(const APIError *err);

// Retryable means transient: network failures, rate limits, 5xx errors
int api_error_is_retryable(long http_status, CURLcode curl_code);

// Global last-error state used by the HTTP client for post-hoc error inspection
void get_last_api_error(APIError *err);
void set_last_api_error(const APIError *err);
void clear_last_api_error(void);

#endif /* API_ERROR_H */
