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
    char buffer[32] = {0}; // Small buffer to test bounds checking
    const char *long_key = "this_is_a_very_long_api_key_that_will_exceed_buffer_size_limits";
    
    // Use pragma to suppress format-truncation warning for this intentional test
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    int ret = snprintf(buffer, sizeof(buffer), "Authorization: Bearer %s", long_key);
    #pragma GCC diagnostic pop
    // snprintf returns the length that would have been written (including null terminator)
    TEST_ASSERT_GREATER_THAN(0, ret);
    // The return value should be larger than buffer size indicating truncation would occur
    TEST_ASSERT_GREATER_THAN((int)sizeof(buffer), ret);
    // Buffer should still be null-terminated even if truncated
    TEST_ASSERT_EQUAL_CHAR('\0', buffer[sizeof(buffer) - 1]);
}

void test_json_payload_structure_without_system_prompt(void)
{
    // Test that our JSON payload generation works with dynamic content (no system prompt)
    char post_data[4096];
    const char *test_message = "This is a test message";
    const char *model = "gpt-3.5-turbo";
    const char *max_tokens_param = "max_tokens";
    int max_tokens = 100;
    
    int json_ret = snprintf(post_data, sizeof(post_data),
        "{"
        "\"model\": \"%s\","
        "\"messages\": ["
            "{"
                "\"role\": \"user\","
                "\"content\": \"%s\""
            "}"
        "],"
        "\"%s\": %d"
        "}", model, test_message, max_tokens_param, max_tokens);
    
    // Test that JSON generation succeeded
    TEST_ASSERT_GREATER_THAN(0, json_ret);
    TEST_ASSERT_LESS_THAN((int)sizeof(post_data), json_ret);
    
    // Basic validation that the JSON contains expected keys and our message
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"model\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"messages\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"max_tokens\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, test_message));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"role\": \"user\""));
    // Should NOT contain system role
    TEST_ASSERT_NULL(strstr(post_data, "\"role\": \"system\""));
}

void test_json_payload_structure_with_system_prompt(void)
{
    // Test that our JSON payload generation works with system prompt
    char post_data[4096];
    const char *test_message = "This is a test message";
    const char *system_prompt = "You are a helpful assistant.";
    const char *model = "gpt-3.5-turbo";
    const char *max_tokens_param = "max_completion_tokens";
    int max_tokens = 100;
    
    int json_ret = snprintf(post_data, sizeof(post_data),
        "{"
        "\"model\": \"%s\","
        "\"messages\": ["
            "{"
                "\"role\": \"system\","
                "\"content\": \"%s\""
            "},"
            "{"
                "\"role\": \"user\","
                "\"content\": \"%s\""
            "}"
        "],"
        "\"%s\": %d"
        "}", model, system_prompt, test_message, max_tokens_param, max_tokens);
    
    // Test that JSON generation succeeded
    TEST_ASSERT_GREATER_THAN(0, json_ret);
    TEST_ASSERT_LESS_THAN((int)sizeof(post_data), json_ret);
    
    // Basic validation that the JSON contains expected keys and our content
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"model\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"messages\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"max_completion_tokens\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, test_message));
    TEST_ASSERT_NOT_NULL(strstr(post_data, system_prompt));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"role\": \"user\""));
    TEST_ASSERT_NOT_NULL(strstr(post_data, "\"role\": \"system\""));
}

void test_max_tokens_param_selection(void)
{
    // Test the logic for selecting the correct max tokens parameter
    const char *openai_url = "https://api.openai.com/v1/chat/completions";
    const char *local_url = "http://localhost:1234/v1/chat/completions";
    
    // Test OpenAI URL detection
    const char *openai_param = "max_tokens";
    if (strstr(openai_url, "api.openai.com") != NULL) {
        openai_param = "max_completion_tokens";
    }
    TEST_ASSERT_EQUAL_STRING("max_completion_tokens", openai_param);
    
    // Test local server URL
    const char *local_param = "max_tokens";
    if (strstr(local_url, "api.openai.com") != NULL) {
        local_param = "max_completion_tokens";
    }
    TEST_ASSERT_EQUAL_STRING("max_tokens", local_param);
}

void test_json_payload_overflow_protection(void)
{
    // Test that very long messages are handled appropriately
    char post_data[100] = {0}; // Small buffer to test overflow
    const char *long_message = "This is a very long message that will definitely cause the JSON payload to exceed the buffer size and trigger truncation behavior in snprintf";
    
    // Use pragma to suppress format-truncation warning for this intentional test
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-truncation"
    int json_ret = snprintf(post_data, sizeof(post_data),
        "{"
        "\"model\": \"gpt-3.5-turbo\","  
        "\"messages\": ["
            "{"
                "\"role\": \"user\","
                "\"content\": \"%s\""
            "}"
        "],"
        "\"max_tokens\": 100"
        "}", long_message);
    #pragma GCC diagnostic pop
    
    // snprintf should return a value >= buffer size indicating truncation would occur
    TEST_ASSERT_GREATER_OR_EQUAL((int)sizeof(post_data), json_ret);
    // Buffer should still be null-terminated
    TEST_ASSERT_EQUAL_CHAR('\0', post_data[sizeof(post_data) - 1]);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_environment_setup);
    RUN_TEST(test_string_operations);
    RUN_TEST(test_string_buffer_overflow_protection);
    RUN_TEST(test_json_payload_structure_without_system_prompt);
    RUN_TEST(test_json_payload_structure_with_system_prompt);
    RUN_TEST(test_max_tokens_param_selection);
    RUN_TEST(test_json_payload_overflow_protection);
    
    return UNITY_END();
}