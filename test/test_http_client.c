#include "unity/unity.h"
#include "../src/http_client.h"
#include <stdlib.h>
#include <string.h>

void setUp(void)
{
    // Set up before each test
}

void tearDown(void)
{
    // Clean up after each test
}

void test_http_response_initialization(void)
{
    struct HTTPResponse response = {0};
    TEST_ASSERT_NULL(response.data);
    TEST_ASSERT_EQUAL_size_t(0, response.size);
}

void test_cleanup_response_with_null_response(void)
{
    // Should not crash with NULL pointer
    cleanup_response(NULL);
    TEST_ASSERT_TRUE(1); // If we get here, the test passed
}

void test_cleanup_response_with_null_data(void)
{
    struct HTTPResponse response = {0};
    cleanup_response(&response);
    TEST_ASSERT_NULL(response.data);
    TEST_ASSERT_EQUAL_size_t(0, response.size);
}

void test_cleanup_response_with_allocated_data(void)
{
    struct HTTPResponse response = {0};
    response.data = malloc(100);
    response.size = 50;
    
    TEST_ASSERT_NOT_NULL(response.data);
    
    cleanup_response(&response);
    TEST_ASSERT_NULL(response.data);
    TEST_ASSERT_EQUAL_size_t(0, response.size);
}

void test_http_post_with_null_url(void)
{
    struct HTTPResponse response = {0};
    int result = http_post(NULL, "test data", &response);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_http_post_with_null_data(void)
{
    struct HTTPResponse response = {0};
    int result = http_post("http://example.com", NULL, &response);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_http_post_with_null_response(void)
{
    int result = http_post("http://example.com", "test data", NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_http_post_with_headers_null_url(void)
{
    struct HTTPResponse response = {0};
    const char *headers[] = {"Content-Type: application/json", NULL};
    int result = http_post_with_headers(NULL, "test data", headers, &response);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_http_post_with_headers_null_data(void)
{
    struct HTTPResponse response = {0};
    const char *headers[] = {"Content-Type: application/json", NULL};
    int result = http_post_with_headers("http://example.com", NULL, headers, &response);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_http_post_with_headers_null_response(void)
{
    const char *headers[] = {"Content-Type: application/json", NULL};
    int result = http_post_with_headers("http://example.com", "test data", headers, NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_http_post_with_headers_null_headers(void)
{
    struct HTTPResponse response = {0};
    // This should work - NULL headers should be handled gracefully
    // Note: This would require network connectivity to actually test successfully
    // For now, we just test that it doesn't crash
    int result = http_post_with_headers("http://httpbin.org/post", "{\"test\": \"data\"}", NULL, &response);
    (void)result; // Suppress unused variable warning
    cleanup_response(&response);
    // We don't assert the result because it depends on network connectivity
    TEST_ASSERT_TRUE(1); // If we get here without crashing, test passes
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_http_response_initialization);
    RUN_TEST(test_cleanup_response_with_null_response);
    RUN_TEST(test_cleanup_response_with_null_data);
    RUN_TEST(test_cleanup_response_with_allocated_data);
    RUN_TEST(test_http_post_with_null_url);
    RUN_TEST(test_http_post_with_null_data);
    RUN_TEST(test_http_post_with_null_response);
    RUN_TEST(test_http_post_with_headers_null_url);
    RUN_TEST(test_http_post_with_headers_null_data);
    RUN_TEST(test_http_post_with_headers_null_response);
    RUN_TEST(test_http_post_with_headers_null_headers);
    
    return UNITY_END();
}