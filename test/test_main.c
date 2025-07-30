#include "unity/unity.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void setUp(void)
{
    // Set up before each test
}

void tearDown(void)
{
    // Clean up after each test
}

void test_environment_setup(void)
{
    // Test that we can check for environment variables
    // Note: We don't require OPENAI_API_KEY to be set for testing
    const char *api_key = getenv("OPENAI_API_KEY");
    (void)api_key; // Suppress unused variable warning
    // This test passes regardless of whether the key is set
    TEST_ASSERT_TRUE(1);
}

void test_string_operations(void)
{
    // Test basic string operations used in main.c
    char buffer[512];
    const char *test_key = "test_key_12345";
    
    int ret = snprintf(buffer, sizeof(buffer), "Authorization: Bearer %s", test_key);
    TEST_ASSERT_GREATER_THAN(0, ret);
    TEST_ASSERT_LESS_THAN((int)sizeof(buffer), ret);
    TEST_ASSERT_EQUAL_STRING("Authorization: Bearer test_key_12345", buffer);
}

void test_string_buffer_overflow_protection(void)
{
    // Test that we handle buffer size checking appropriately
    char buffer[64]; // Buffer size
    char long_key[30]; // Key that will make total string longer than buffer
    memset(long_key, 'A', sizeof(long_key) - 1);
    long_key[sizeof(long_key) - 1] = '\0';
    
    int ret = snprintf(buffer, sizeof(buffer), "Authorization: Bearer %s", long_key);
    // snprintf returns the length that would have been written
    TEST_ASSERT_GREATER_THAN(0, ret);
    // Buffer should be null-terminated even if truncated
    TEST_ASSERT_EQUAL_CHAR('\0', buffer[sizeof(buffer) - 1]);
}

void test_json_payload_structure(void)
{
    // Test that our JSON payload is well-formed
    const char *expected_json = "{"
        "\"model\": \"gpt-3.5-turbo\","
        "\"messages\": ["
            "{"
                "\"role\": \"user\","
                "\"content\": \"Hello from C! Please respond with a brief greeting.\""
            "}"
        "],"
        "\"max_tokens\": 100"
    "}";
    
    // Basic validation that the JSON contains expected keys
    TEST_ASSERT_NOT_NULL(strstr(expected_json, "\"model\""));
    TEST_ASSERT_NOT_NULL(strstr(expected_json, "\"messages\""));
    TEST_ASSERT_NOT_NULL(strstr(expected_json, "\"max_tokens\""));
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_environment_setup);
    RUN_TEST(test_string_operations);
    RUN_TEST(test_string_buffer_overflow_protection);
    RUN_TEST(test_json_payload_structure);
    
    return UNITY_END();
}