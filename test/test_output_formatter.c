#include "unity.h"
#include "output_formatter.h"
#include <string.h>
#include <stdio.h>

// Test data - typical OpenAI response
static const char *openai_response = 
    "{"
    "\"id\":\"chatcmpl-123\","
    "\"object\":\"chat.completion\","
    "\"created\":1677652288,"
    "\"model\":\"gpt-3.5-turbo\","
    "\"choices\":[{"
        "\"index\":0,"
        "\"message\":{"
            "\"role\":\"assistant\","
            "\"content\":\"Hello! How can I help you today?\""
        "},"
        "\"finish_reason\":\"stop\""
    "}],"
    "\"usage\":{"
        "\"prompt_tokens\":9,"
        "\"completion_tokens\":12,"
        "\"total_tokens\":21"
    "}"
    "}";

// Test data - DeepSeek response with thinking
static const char *deepseek_response = 
    "{"
    "\"id\":\"chatcmpl-qkg9p3de9npcjqwrkingtq\","
    "\"object\":\"chat.completion\","
    "\"created\":1753895581,"
    "\"model\":\"deepseek/deepseek-r1-0528-qwen3-8b\","
    "\"choices\":[{"
        "\"index\":0,"
        "\"logprobs\":null,"
        "\"finish_reason\":\"length\","
        "\"message\":{"
            "\"role\":\"assistant\","
            "\"content\":\"<think>\\nUser is asking about my identity.\\n</think>\\n\\nI am DeepSeek R1, an AI assistant.\""
        "}"
    "}],"
    "\"usage\":{"
        "\"prompt_tokens\":13,"
        "\"completion_tokens\":99,"
        "\"total_tokens\":112"
    "},"
    "\"stats\":{},"
    "\"system_fingerprint\":\"deepseek/deepseek-r1-0528-qwen3-8b\""
    "}";

// Test data - minimal response without usage
static const char *minimal_response = 
    "{"
    "\"choices\":[{"
        "\"message\":{"
            "\"content\":\"Simple response\""
        "}"
    "}]"
    "}";

// Test data - tool calls response (no content field)
static const char *tool_calls_response = 
    "{"
    "\"id\":\"chatcmpl-test123\","
    "\"object\":\"chat.completion\","
    "\"created\":1753923401,"
    "\"model\":\"test/model\","
    "\"choices\":[{"
        "\"index\":0,"
        "\"message\":{"
            "\"role\":\"assistant\","
            "\"tool_calls\":[{"
                "\"id\":\"call_123\","
                "\"type\":\"function\","
                "\"function\":{"
                    "\"name\":\"shell_execute\","
                    "\"arguments\":\"{\\\"command\\\":\\\"echo test\\\"}\""
                "}"
            "}]"
        "},"
        "\"finish_reason\":\"tool_calls\""
    "}],"
    "\"usage\":{"
        "\"prompt_tokens\":100,"
        "\"completion_tokens\":25,"
        "\"total_tokens\":125"
    "}"
    "}";

// Test data - malformed JSON
static const char *malformed_response = 
    "{"
    "\"choices\":[{"
        "\"message\":{"
            "\"content\":\"Incomplete";

void setUp(void) {
    // Setup before each test
}

void tearDown(void) {
    // Cleanup after each test
}

void test_parse_api_response_with_null_parameters(void) {
    ParsedResponse result;
    
    // Test null JSON response
    TEST_ASSERT_EQUAL(-1, parse_api_response(NULL, &result));
    
    // Test null result pointer
    TEST_ASSERT_EQUAL(-1, parse_api_response(openai_response, NULL));
}

void test_parse_api_response_openai_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(openai_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // OpenAI doesn't have thinking
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Hello! How can I help you today?", result.response_content);
    TEST_ASSERT_EQUAL(9, result.prompt_tokens);
    TEST_ASSERT_EQUAL(12, result.completion_tokens);
    TEST_ASSERT_EQUAL(21, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_deepseek_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(deepseek_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("\nUser is asking about my identity.\n", result.thinking_content);
    TEST_ASSERT_EQUAL_STRING("I am DeepSeek R1, an AI assistant.", result.response_content);
    TEST_ASSERT_EQUAL(13, result.prompt_tokens);
    TEST_ASSERT_EQUAL(99, result.completion_tokens);
    TEST_ASSERT_EQUAL(112, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_minimal_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(minimal_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // No thinking in minimal response
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Simple response", result.response_content);
    TEST_ASSERT_EQUAL(-1, result.prompt_tokens);
    TEST_ASSERT_EQUAL(-1, result.completion_tokens);
    TEST_ASSERT_EQUAL(-1, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_malformed_json(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(malformed_response, &result);
    
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_parse_api_response_no_content(void) {
    const char *no_content_response = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"role\":\"assistant\""
            "}"
        "}]"
        "}";
    
    ParsedResponse result;
    
    int ret = parse_api_response(no_content_response, &result);
    
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_cleanup_parsed_response_with_null_pointer(void) {
    // Should not crash
    cleanup_parsed_response(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_cleanup_parsed_response_with_null_content(void) {
    ParsedResponse result = {0};
    result.thinking_content = NULL;
    result.response_content = NULL;
    
    // Should not crash
    cleanup_parsed_response(&result);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_cleanup_parsed_response_with_allocated_content(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(openai_response, &result);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);
    
    cleanup_parsed_response(&result);
    TEST_ASSERT_NULL(result.thinking_content);
    TEST_ASSERT_NULL(result.response_content);
}

void test_print_formatted_response_with_null_parameters(void) {
    // Should not crash
    print_formatted_response(NULL);
    
    ParsedResponse result = {0};
    result.thinking_content = NULL;
    result.response_content = NULL;
    print_formatted_response(&result);
    
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_content_with_escaped_quotes(void) {
    const char *escaped_response = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"He said \\\"Hello world!\\\" to me.\""
            "}"
        "}],"
        "\"usage\":{"
            "\"total_tokens\":15"
        "}"
        "}";
    
    ParsedResponse result;
    
    int ret = parse_api_response(escaped_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // No thinking in this test
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("He said \"Hello world!\" to me.", result.response_content);
    TEST_ASSERT_EQUAL(15, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_api_response_tool_calls_format(void) {
    ParsedResponse result;
    
    int ret = parse_api_response(tool_calls_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);  // No content in tool calls
    TEST_ASSERT_NULL(result.response_content);  // No content in tool calls
    TEST_ASSERT_EQUAL(100, result.prompt_tokens);
    TEST_ASSERT_EQUAL(25, result.completion_tokens);
    TEST_ASSERT_EQUAL(125, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

// Anthropic response tests
void test_parse_anthropic_response_basic(void) {
    const char *anthropic_response = 
        "{"
        "\"content\": [{\"type\": \"text\", \"text\": \"Hello from Anthropic!\"}],"
        "\"usage\": {\"input_tokens\": 10, \"output_tokens\": 5}"
        "}";
    
    ParsedResponse result;
    int ret = parse_anthropic_response(anthropic_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Hello from Anthropic!", result.response_content);
    TEST_ASSERT_EQUAL(10, result.prompt_tokens);
    TEST_ASSERT_EQUAL(5, result.completion_tokens);
    TEST_ASSERT_EQUAL(15, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_anthropic_response_with_thinking(void) {
    const char *anthropic_response = 
        "{"
        "\"content\": [{\"type\": \"text\", \"text\": \"<think>I need to think about this.</think>\\n\\nThe answer is 42.\"}],"
        "\"usage\": {\"input_tokens\": 20, \"output_tokens\": 15}"
        "}";
    
    ParsedResponse result;
    int ret = parse_anthropic_response(anthropic_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("I need to think about this.", result.thinking_content);
    TEST_ASSERT_EQUAL_STRING("The answer is 42.", result.response_content);
    TEST_ASSERT_EQUAL(20, result.prompt_tokens);
    TEST_ASSERT_EQUAL(15, result.completion_tokens);
    TEST_ASSERT_EQUAL(35, result.total_tokens);
    
    cleanup_parsed_response(&result);
}

void test_parse_anthropic_response_null_parameters(void) {
    ParsedResponse result;
    
    // Test null JSON response
    TEST_ASSERT_EQUAL(-1, parse_anthropic_response(NULL, &result));
    
    // Test null result pointer
    const char *valid_response = "{\"content\": [{\"type\": \"text\", \"text\": \"test\"}]}";
    TEST_ASSERT_EQUAL(-1, parse_anthropic_response(valid_response, NULL));
}

void test_parse_anthropic_response_malformed(void) {
    const char *malformed_response = "{\"invalid\": \"json structure\"}";
    
    ParsedResponse result;
    int ret = parse_anthropic_response(malformed_response, &result);
    
    TEST_ASSERT_EQUAL(-1, ret);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_parse_api_response_with_null_parameters);
    RUN_TEST(test_parse_api_response_openai_format);
    RUN_TEST(test_parse_api_response_deepseek_format);
    RUN_TEST(test_parse_api_response_minimal_format);
    RUN_TEST(test_parse_api_response_malformed_json);
    RUN_TEST(test_parse_api_response_no_content);
    RUN_TEST(test_cleanup_parsed_response_with_null_pointer);
    RUN_TEST(test_cleanup_parsed_response_with_null_content);
    RUN_TEST(test_cleanup_parsed_response_with_allocated_content);
    RUN_TEST(test_print_formatted_response_with_null_parameters);
    RUN_TEST(test_content_with_escaped_quotes);
    RUN_TEST(test_parse_api_response_tool_calls_format);
    
    // Anthropic tests
    RUN_TEST(test_parse_anthropic_response_basic);
    RUN_TEST(test_parse_anthropic_response_with_thinking);
    RUN_TEST(test_parse_anthropic_response_null_parameters);
    RUN_TEST(test_parse_anthropic_response_malformed);
    
    return UNITY_END();
}