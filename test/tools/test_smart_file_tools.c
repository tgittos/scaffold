#include "unity.h"
#include "file_tools.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // Clean up any existing test files
}

void tearDown(void) {
    // Clean up test files
    unlink("test_smart_read.c");
    unlink("test_smart_large.c");
}

void test_estimate_content_tokens(void) {
    // Test with simple text
    const char *simple_text = "Hello world this is a test";
    int tokens = estimate_content_tokens(simple_text);
    TEST_ASSERT_TRUE(tokens > 0);
    TEST_ASSERT_TRUE(tokens < 20); // Should be around 5-6 tokens
    
    // Test with code (should be more efficient)
    const char *code_text = "int main() {\n    printf(\"Hello world\");\n    return 0;\n}";
    int code_tokens = estimate_content_tokens(code_text);
    TEST_ASSERT_TRUE(code_tokens > 0);
    
    // Code should be more efficiently tokenized (fewer tokens per char)
    float simple_ratio = (float)strlen(simple_text) / tokens;
    float code_ratio = (float)strlen(code_text) / code_tokens;
    TEST_ASSERT_TRUE(code_ratio > simple_ratio);
}

void test_smart_truncate_content(void) {
    const char *test_content = 
        "#include <stdio.h>\n\n"
        "int helper_function(int x) {\n"
        "    return x * 2;\n"
        "}\n\n"
        "int main() {\n"
        "    printf(\"Hello world\");\n"
        "    int result = helper_function(5);\n"
        "    printf(\"Result: %d\\n\", result);\n"
        "    return 0;\n"
        "}\n";
    
    char *truncated = NULL;
    int was_truncated = 0;
    
    // Test with reasonable limit (should not truncate)
    int result = smart_truncate_content(test_content, 100, &truncated, &was_truncated);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NOT_NULL(truncated);
    TEST_ASSERT_EQUAL_INT(0, was_truncated);
    TEST_ASSERT_EQUAL_STRING(test_content, truncated);
    free(truncated);
    
    // Test with very small limit (should truncate)
    result = smart_truncate_content(test_content, 10, &truncated, &was_truncated);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NOT_NULL(truncated);
    TEST_ASSERT_EQUAL_INT(1, was_truncated);
    TEST_ASSERT_TRUE(strlen(truncated) < strlen(test_content));
    TEST_ASSERT_TRUE(strstr(truncated, "Content truncated") != NULL);
    free(truncated);
}

void test_file_read_content_smart(void) {
    // Create a test file with some code
    FILE *test_file = fopen("test_smart_read.c", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    
    const char *file_content = 
        "#include <stdio.h>\n\n"
        "// This is a test function\n"
        "int test_function(int a, int b) {\n"
        "    printf(\"Adding %d + %d\\n\", a, b);\n"
        "    return a + b;\n"
        "}\n\n"
        "int main() {\n"
        "    int result = test_function(3, 4);\n"
        "    printf(\"Result: %d\\n\", result);\n"
        "    return 0;\n"
        "}\n";
    
    fprintf(test_file, "%s", file_content);
    fclose(test_file);
    
    // Test reading with no token limit
    char *content = NULL;
    int truncated = 0;
    FileErrorCode error = file_read_content_smart("test_smart_read.c", 0, 0, 0, &content, &truncated);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, error);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_EQUAL_INT(0, truncated);
    TEST_ASSERT_EQUAL_STRING(file_content, content);
    free(content);
    
    // Test reading with small token limit
    error = file_read_content_smart("test_smart_read.c", 0, 0, 20, &content, &truncated);
    
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, error);
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_EQUAL_INT(1, truncated);
    TEST_ASSERT_TRUE(strlen(content) < strlen(file_content));
    free(content);
}

void test_file_read_tool_call_with_max_tokens(void) {
    // Create a test file
    FILE *test_file = fopen("test_smart_large.c", "w");
    TEST_ASSERT_NOT_NULL(test_file);
    
    // Write a larger file that would definitely exceed a small token limit
    for (int i = 0; i < 50; i++) {
        fprintf(test_file, "// Line %d: This is a comment line with some text\n", i);
        fprintf(test_file, "int function_%d() { return %d; }\n\n", i, i);
    }
    fclose(test_file);
    
    // Test tool call without max_tokens
    ToolCall call;
    call.id = strdup("test_call_1");
    call.name = strdup("file_read");
    call.arguments = strdup("{\"file_path\": \"test_smart_large.c\"}");
    
    ToolResult result;
    int exec_result = execute_file_read_tool_call(&call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "\"truncated\": false") != NULL);
    
    free(call.id);
    free(call.name);
    free(call.arguments);
    free(result.tool_call_id);
    free(result.result);
    
    // Test tool call with max_tokens
    call.id = strdup("test_call_2");
    call.name = strdup("file_read");
    call.arguments = strdup("{\"file_path\": \"test_smart_large.c\", \"max_tokens\": 50}");
    
    exec_result = execute_file_read_tool_call(&call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "\"truncated\": true") != NULL);
    
    free(call.id);
    free(call.name);
    free(call.arguments);
    free(result.tool_call_id);
    free(result.result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_estimate_content_tokens);
    RUN_TEST(test_smart_truncate_content);
    RUN_TEST(test_file_read_content_smart);
    RUN_TEST(test_file_read_tool_call_with_max_tokens);
    
    return UNITY_END();
}