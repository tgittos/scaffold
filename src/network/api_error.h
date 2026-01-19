#ifndef API_ERROR_H
#define API_ERROR_H

#include <curl/curl.h>

/**
 * Structure to hold API error information for enhanced error reporting
 */
typedef struct {
    int is_retryable;           // Whether this error can be retried
    int attempts_made;          // Number of attempts made (including initial)
    long http_status;           // HTTP status code (0 if network error)
    CURLcode curl_code;         // CURL error code
    char error_message[256];    // Detailed error message
} APIError;

/**
 * Initialize an APIError structure
 *
 * @param err Pointer to APIError to initialize
 */
void api_error_init(APIError *err);

/**
 * Set API error details
 *
 * @param err Pointer to APIError to populate
 * @param http_status HTTP status code
 * @param curl_code CURL error code
 * @param attempts Number of attempts made
 */
void api_error_set(APIError *err, long http_status, CURLcode curl_code, int attempts);

/**
 * Get a user-friendly error message based on error type
 *
 * @param err Pointer to APIError
 * @return User-friendly error message string
 */
const char* api_error_user_message(const APIError *err);

/**
 * Check if an error is retryable based on HTTP status and CURL code
 *
 * @param http_status HTTP status code
 * @param curl_code CURL error code
 * @return 1 if retryable, 0 otherwise
 */
int api_error_is_retryable(long http_status, CURLcode curl_code);

/**
 * Get the last API error from the HTTP client
 *
 * @param err Pointer to APIError to populate
 */
void get_last_api_error(APIError *err);

/**
 * Set the last API error (called internally by HTTP client)
 *
 * @param err Pointer to APIError with error details
 */
void set_last_api_error(const APIError *err);

/**
 * Clear the last API error
 */
void clear_last_api_error(void);

#endif /* API_ERROR_H */
