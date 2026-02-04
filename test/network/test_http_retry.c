#include "../unity/unity.h"
#include "network/api_error.h"
#include "../../src/utils/config.h"
#include <string.h>
#include <curl/curl.h>

void setUp(void) {
    // Initialize config system for each test
    config_init();
}

void tearDown(void) {
    // Cleanup config
    config_cleanup();
}

// Test api_error_is_retryable with various CURL codes
void test_api_error_retryable_curl_codes(void) {
    // Network errors should be retryable
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(0, CURLE_COULDNT_CONNECT));
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(0, CURLE_OPERATION_TIMEDOUT));
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(0, CURLE_GOT_NOTHING));
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(0, CURLE_RECV_ERROR));
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(0, CURLE_SEND_ERROR));

    // Other CURL errors should not be retryable
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(0, CURLE_SSL_CONNECT_ERROR));
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(0, CURLE_URL_MALFORMAT));
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(0, CURLE_COULDNT_RESOLVE_HOST));
}

// Test api_error_is_retryable with various HTTP status codes
void test_api_error_retryable_http_status(void) {
    // Transient HTTP errors should be retryable
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(429, CURLE_OK));  // Rate limited
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(502, CURLE_OK));  // Bad gateway
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(503, CURLE_OK));  // Service unavailable
    TEST_ASSERT_EQUAL(1, api_error_is_retryable(504, CURLE_OK));  // Gateway timeout

    // Permanent HTTP errors should not be retryable
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(400, CURLE_OK));  // Bad request
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(401, CURLE_OK));  // Unauthorized
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(403, CURLE_OK));  // Forbidden
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(404, CURLE_OK));  // Not found
    TEST_ASSERT_EQUAL(0, api_error_is_retryable(500, CURLE_OK));  // Internal server error
}

// Test api_error_init
void test_api_error_init(void) {
    APIError err;
    err.is_retryable = 1;
    err.attempts_made = 5;
    err.http_status = 500;
    err.curl_code = CURLE_SSL_CONNECT_ERROR;
    strcpy(err.error_message, "test error");

    api_error_init(&err);

    TEST_ASSERT_EQUAL(0, err.is_retryable);
    TEST_ASSERT_EQUAL(0, err.attempts_made);
    TEST_ASSERT_EQUAL(0, err.http_status);
    TEST_ASSERT_EQUAL(CURLE_OK, err.curl_code);
    TEST_ASSERT_EQUAL_STRING("", err.error_message);
}

// Test api_error_set
void test_api_error_set(void) {
    APIError err;
    api_error_init(&err);

    // Test with rate limit error
    api_error_set(&err, 429, CURLE_OK, 3);
    TEST_ASSERT_EQUAL(1, err.is_retryable);
    TEST_ASSERT_EQUAL(3, err.attempts_made);
    TEST_ASSERT_EQUAL(429, err.http_status);
    TEST_ASSERT_EQUAL(CURLE_OK, err.curl_code);
    TEST_ASSERT_TRUE(strstr(err.error_message, "429") != NULL);

    // Test with CURL error
    api_error_set(&err, 0, CURLE_OPERATION_TIMEDOUT, 2);
    TEST_ASSERT_EQUAL(1, err.is_retryable);
    TEST_ASSERT_EQUAL(2, err.attempts_made);
    TEST_ASSERT_EQUAL(0, err.http_status);
    TEST_ASSERT_EQUAL(CURLE_OPERATION_TIMEDOUT, err.curl_code);
    TEST_ASSERT_TRUE(strstr(err.error_message, "CURL") != NULL);

    // Test with non-retryable error
    api_error_set(&err, 401, CURLE_OK, 1);
    TEST_ASSERT_EQUAL(0, err.is_retryable);
    TEST_ASSERT_EQUAL(1, err.attempts_made);
}

// Test api_error_user_message for CURL errors
void test_api_error_user_message_curl(void) {
    APIError err;
    api_error_init(&err);
    const char *msg;

    // Connection error
    api_error_set(&err, 0, CURLE_COULDNT_CONNECT, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "connect") != NULL || strstr(msg, "Connect") != NULL);

    // Timeout error
    api_error_set(&err, 0, CURLE_OPERATION_TIMEDOUT, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "timed out") != NULL || strstr(msg, "Timed out") != NULL);

    // SSL error
    api_error_set(&err, 0, CURLE_SSL_CONNECT_ERROR, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "SSL") != NULL || strstr(msg, "secure") != NULL);
}

// Test api_error_user_message for HTTP errors
void test_api_error_user_message_http(void) {
    APIError err;
    api_error_init(&err);
    const char *msg;

    // Rate limit
    api_error_set(&err, 429, CURLE_OK, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "Rate") != NULL || strstr(msg, "rate") != NULL);

    // Authentication
    api_error_set(&err, 401, CURLE_OK, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "API key") != NULL || strstr(msg, "Authentication") != NULL);

    // Forbidden
    api_error_set(&err, 403, CURLE_OK, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "permission") != NULL || strstr(msg, "forbidden") != NULL ||
                     strstr(msg, "Forbidden") != NULL);

    // Server error
    api_error_set(&err, 500, CURLE_OK, 1);
    msg = api_error_user_message(&err);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "server") != NULL || strstr(msg, "Server") != NULL);
}

// Test get/set/clear last API error
void test_api_error_last_error(void) {
    APIError err1, err2;
    api_error_init(&err1);
    api_error_init(&err2);

    // Set an error
    api_error_set(&err1, 429, CURLE_OK, 3);
    set_last_api_error(&err1);

    // Retrieve it
    get_last_api_error(&err2);
    TEST_ASSERT_EQUAL(429, err2.http_status);
    TEST_ASSERT_EQUAL(3, err2.attempts_made);
    TEST_ASSERT_EQUAL(1, err2.is_retryable);

    // Clear it
    clear_last_api_error();
    get_last_api_error(&err2);
    TEST_ASSERT_EQUAL(0, err2.http_status);
    TEST_ASSERT_EQUAL(0, err2.attempts_made);
}

// Test config defaults for retry settings
void test_retry_config_defaults(void) {
    // Test that defaults are returned when no config is loaded
    int max_retries = config_get_int("api_max_retries", 99);
    int delay_ms = config_get_int("api_retry_delay_ms", 99);
    float backoff = config_get_float("api_backoff_factor", 99.0f);

    // Should get actual default values (3, 1000, 2.0) not the fallback (99)
    TEST_ASSERT_EQUAL(3, max_retries);
    TEST_ASSERT_EQUAL(1000, delay_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 2.0f, backoff);
}

// Test NULL handling
void test_api_error_null_handling(void) {
    // These should not crash
    api_error_init(NULL);
    api_error_set(NULL, 200, CURLE_OK, 1);
    set_last_api_error(NULL);
    get_last_api_error(NULL);

    // Should return a default message
    const char *msg = api_error_user_message(NULL);
    TEST_ASSERT_NOT_NULL(msg);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_api_error_retryable_curl_codes);
    RUN_TEST(test_api_error_retryable_http_status);
    RUN_TEST(test_api_error_init);
    RUN_TEST(test_api_error_set);
    RUN_TEST(test_api_error_user_message_curl);
    RUN_TEST(test_api_error_user_message_http);
    RUN_TEST(test_api_error_last_error);
    RUN_TEST(test_retry_config_defaults);
    RUN_TEST(test_api_error_null_handling);

    return UNITY_END();
}
