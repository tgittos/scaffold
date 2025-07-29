#include "unity/unity.h"
#include "http_client.h"
#include <stdio.h>
#include <string.h>
#include <curl/curl.h>

void setUp(void) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void tearDown(void) {
    curl_global_cleanup();
}

void test_http_post_null_parameters(void) {
    struct HTTPResponse response = {0};
    
    TEST_ASSERT_EQUAL_INT(-1, http_post(NULL, "data", &response));
    TEST_ASSERT_EQUAL_INT(-1, http_post("http://example.com", NULL, &response));
    TEST_ASSERT_EQUAL_INT(-1, http_post("http://example.com", "data", NULL));
}

void test_cleanup_response_null_safe(void) {
    cleanup_response(NULL);
    
    struct HTTPResponse response = {0};
    cleanup_response(&response);
    
    TEST_ASSERT_TRUE(1);
}

void test_cleanup_response_with_data(void) {
    struct HTTPResponse response = {0};
    response.data = malloc(10);
    response.size = 10;
    
    TEST_ASSERT_NOT_NULL(response.data);
    
    cleanup_response(&response);
    
    TEST_ASSERT_NULL(response.data);
    TEST_ASSERT_EQUAL_size_t(0, response.size);
}

void test_http_post_invalid_url(void) {
    struct HTTPResponse response = {0};
    const char *invalid_url = "not-a-valid-url";
    const char *post_data = "test data";
    
    int result = http_post(invalid_url, post_data, &response);
    
    TEST_ASSERT_EQUAL_INT(-1, result);
    cleanup_response(&response);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_http_post_null_parameters);
    RUN_TEST(test_cleanup_response_null_safe);
    RUN_TEST(test_cleanup_response_with_data);
    RUN_TEST(test_http_post_invalid_url);
    
    return UNITY_END();
}