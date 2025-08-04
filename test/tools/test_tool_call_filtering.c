#include "unity.h"
#include "output_formatter.h"
#include <string.h>
#include <stdio.h>

void setUp(void) {
    // Empty setup function required by Unity
}

void tearDown(void) {
    // Empty teardown function required by Unity
}

void test_tool_call_filtering_complete_workflow(void) {
    // Test data simulating a local model response with tool call markup
    const char *local_model_json = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"I'll help you read that file. <tool_call>{\\\"name\\\": \\\"file_read\\\", \\\"arguments\\\": {\\\"file_path\\\": \\\"/test/example.txt\\\"}}</tool_call> The file should contain the information you need.\""
            "}"
        "}],"
        "\"usage\":{"
            "\"total_tokens\":45"
        "}"
        "}";
    
    ParsedResponse result;
    int ret = parse_api_response(local_model_json, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);
    
    // Verify the tool call markup has been filtered out
    TEST_ASSERT_NULL(strstr(result.response_content, "<tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "</tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "file_read"));
    TEST_ASSERT_NULL(strstr(result.response_content, "file_path"));
    
    // Verify the descriptive text remains
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "I'll help you read that file."));
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "The file should contain"));
    
    // Verify content is clean (no extra spaces where tool call was removed)
    char *expected = "I'll help you read that file.  The file should contain the information you need.";
    TEST_ASSERT_EQUAL_STRING(expected, result.response_content);
    
    cleanup_parsed_response(&result);
}

void test_tool_call_filtering_multiple_calls(void) {
    // Test with multiple tool calls in the same response
    const char *multi_tool_response = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"Let me check two things: <tool_call>{\\\"name\\\": \\\"file_read\\\", \\\"arguments\\\": {\\\"path\\\": \\\"file1.txt\\\"}}</tool_call> and also <tool_call>{\\\"name\\\": \\\"file_list\\\", \\\"arguments\\\": {\\\"dir\\\": \\\"./\\\"}}</tool_call> to see what we have.\""
            "}"
        "}]"
        "}";
    
    ParsedResponse result;
    int ret = parse_api_response(multi_tool_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);
    
    // Verify both tool calls are filtered out
    TEST_ASSERT_NULL(strstr(result.response_content, "<tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "</tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "file_read"));
    TEST_ASSERT_NULL(strstr(result.response_content, "file_list"));
    
    // Verify descriptive text remains
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "Let me check two things:"));
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "and also"));
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "to see what we have."));
    
    char *expected = "Let me check two things:  and also  to see what we have.";
    TEST_ASSERT_EQUAL_STRING(expected, result.response_content);
    
    cleanup_parsed_response(&result);
}

void test_tool_call_filtering_malformed_tags(void) {
    // Test with malformed tool call tags (should be preserved)
    const char *malformed_response = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"Here's some text with <tool_call> that doesn't close properly and some normal content.\""
            "}"
        "}]"
        "}";
    
    ParsedResponse result;
    int ret = parse_api_response(malformed_response, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.response_content);
    
    // Malformed tags should be preserved (no closing tag)
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "<tool_call>"));
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "normal content"));
    
    cleanup_parsed_response(&result);
}

void test_tool_call_filtering_with_thinking_tags(void) {
    // Test filtering works with thinking tags too
    const char *thinking_with_tools = 
        "{"
        "\"choices\":[{"
            "\"message\":{"
                "\"content\":\"<think>I need to read a file for the user.</think>\\n\\nI'll read the file for you. <tool_call>{\\\"name\\\": \\\"file_read\\\", \\\"arguments\\\": {\\\"file_path\\\": \\\"/test.txt\\\"}}</tool_call> This should give us the information.\""
            "}"
        "}]"
        "}";
    
    ParsedResponse result;
    int ret = parse_api_response(thinking_with_tools, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_NULL(result.thinking_content);
    TEST_ASSERT_NOT_NULL(result.response_content);
    
    // Thinking content should be preserved
    TEST_ASSERT_EQUAL_STRING("I need to read a file for the user.", result.thinking_content);
    
    // Response content should have tool calls filtered out
    TEST_ASSERT_NULL(strstr(result.response_content, "<tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "</tool_call>"));
    TEST_ASSERT_NULL(strstr(result.response_content, "file_read"));
    
    // But descriptive text should remain
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "I'll read the file"));
    TEST_ASSERT_NOT_NULL(strstr(result.response_content, "This should give us"));
    
    cleanup_parsed_response(&result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_tool_call_filtering_complete_workflow);
    RUN_TEST(test_tool_call_filtering_multiple_calls);
    RUN_TEST(test_tool_call_filtering_malformed_tags);
    RUN_TEST(test_tool_call_filtering_with_thinking_tags);
    
    return UNITY_END();
}