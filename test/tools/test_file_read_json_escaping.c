#include "unity/unity.h"
#include "file_tools.h"
#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // This is run before each test
}

void tearDown(void) {
    // This is run after each test
}

// Test that file reading handles files with JSON-breaking characters correctly
void test_file_read_with_json_breaking_characters(void) {
    const char *test_file = "/tmp/test_json_breaking.txt";
    const char *test_content = "This file contains \"quotes\" and\nnewlines\nand \\backslashes\\ and other JSON-breaking content";
    
    // Create test file
    FILE *f = fopen(test_file, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(test_content, f);
    fclose(f);
    
    // Create a tool call for reading the file
    ToolCall tool_call = {
        .id = "test_123",
        .name = "file_read",
        .arguments = "{\"file_path\": \"/tmp/test_json_breaking.txt\"}"
    };
    
    ToolResult result = {0};
    
    // Execute the tool call
    int exec_result = execute_file_read_tool_call(&tool_call, &result);
    
    // Should succeed
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(result.tool_call_id);
    
    // The result should be valid JSON even with problematic content
    TEST_ASSERT_TRUE(strstr(result.result, "\"success\": true") != NULL);
    TEST_ASSERT_TRUE(strstr(result.result, "\"file_path\":") != NULL);
    TEST_ASSERT_TRUE(strstr(result.result, "\"content\":") != NULL);
    
    // Test that the JSON is actually parseable (basic validation)
    int quote_count = 0;
    int brace_count = 0;
    char *p = result.result;
    while (*p) {
        if (*p == '"' && (p == result.result || *(p-1) != '\\')) quote_count++;
        if (*p == '{') brace_count++;
        if (*p == '}') brace_count--;
        p++;
    }
    // Should have even number of quotes and balanced braces
    TEST_ASSERT_EQUAL_INT(0, quote_count % 2);
    TEST_ASSERT_EQUAL_INT(0, brace_count);
    
    // Clean up
    free(result.result);
    free(result.tool_call_id);
    unlink(test_file);
}

// Test that reproduces the specific bug from the conversation log
void test_file_read_makefile_bug(void) {
    
    // Create a tool call for reading the Makefile
    ToolCall tool_call = {
        .id = "test_makefile",
        .name = "file_read", 
        .arguments = "{\"file_path\": \"./Makefile\"}"
    };
    
    ToolResult result = {0};
    
    // Execute the tool call  
    int exec_result = execute_file_read_tool_call(&tool_call, &result);
    
    // Should succeed
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(result.tool_call_id);
    
    // The result should be valid JSON and contain actual content
    TEST_ASSERT_TRUE(strstr(result.result, "\"success\": true") != NULL);
    TEST_ASSERT_TRUE(strstr(result.result, "\"content\":") != NULL);
    
    // Should contain some Makefile content indicators
    TEST_ASSERT_TRUE(strstr(result.result, "Makefile") != NULL || 
                     strstr(result.result, "CC") != NULL ||
                     strstr(result.result, "CFLAGS") != NULL);
    
    // Clean up
    free(result.result);
    free(result.tool_call_id);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_file_read_with_json_breaking_characters);
    RUN_TEST(test_file_read_makefile_bug);
    
    return UNITY_END();
}