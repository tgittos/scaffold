#include "unity.h"
#include "tools/file_tools.h"
#include "tools/tools_system.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

void test_file_write_with_escaped_content(void) {
    // Create a tool call with escaped JSON content
    ToolCall tool_call = {
        .id = "test_id",
        .name = "file_write",
        .arguments = "{\"file_path\": \"/tmp/test_escaped.c\", \"content\": \"#include <stdio.h>\\n\\nint main() {\\n    printf(\\\"Hello, World!\\\\n\\\");\\n    return 0;\\n}\\n\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_file_write_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    // Verify the file was written correctly
    char *read_content = NULL;
    FileErrorCode read_result = file_read_content("/tmp/test_escaped.c", 0, 0, &read_content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, read_result);
    TEST_ASSERT_NOT_NULL(read_content);
    
    // Expected content with proper newlines
    const char *expected_content = "#include <stdio.h>\n\nint main() {\n    printf(\"Hello, World!\\n\");\n    return 0;\n}\n";
    TEST_ASSERT_EQUAL_STRING(expected_content, read_content);
    
    // Cleanup
    free(read_content);
    free(result.tool_call_id);
    free(result.result);
    unlink("/tmp/test_escaped.c");
}

void test_file_append_with_escaped_content(void) {
    // First write initial content
    const char *initial_content = "// Initial content\n";
    file_write_content("/tmp/test_append_escaped.c", initial_content, 0);
    
    // Create a tool call with escaped JSON content for append
    ToolCall tool_call = {
        .id = "test_append_id",
        .name = "file_append",
        .arguments = "{\"file_path\": \"/tmp/test_append_escaped.c\", \"content\": \"\\nvoid test_function() {\\n    // This is a test\\n}\\n\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_file_append_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    // Verify the file contains both initial and appended content
    char *read_content = NULL;
    FileErrorCode read_result = file_read_content("/tmp/test_append_escaped.c", 0, 0, &read_content);
    TEST_ASSERT_EQUAL_INT(FILE_SUCCESS, read_result);
    TEST_ASSERT_NOT_NULL(read_content);
    
    const char *expected_content = "// Initial content\n\nvoid test_function() {\n    // This is a test\n}\n";
    TEST_ASSERT_EQUAL_STRING(expected_content, read_content);
    
    // Cleanup
    free(read_content);
    free(result.tool_call_id);
    free(result.result);
    unlink("/tmp/test_append_escaped.c");
}

void test_file_write_handles_backslashes_correctly(void) {
    // Test with content containing backslashes
    ToolCall tool_call = {
        .id = "test_backslash",
        .name = "file_write",
        .arguments = "{\"file_path\": \"/tmp/test_backslash.txt\", \"content\": \"Path: C:\\\\Users\\\\Test\\\\file.txt\\nRegex: \\\\d{3}-\\\\d{4}\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_file_write_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    // Verify content
    char *read_content = NULL;
    file_read_content("/tmp/test_backslash.txt", 0, 0, &read_content);
    
    const char *expected_content = "Path: C:\\Users\\Test\\file.txt\nRegex: \\d{3}-\\d{4}";
    TEST_ASSERT_EQUAL_STRING(expected_content, read_content);
    
    // Cleanup
    free(read_content);
    free(result.tool_call_id);
    free(result.result);
    unlink("/tmp/test_backslash.txt");
}

void test_file_write_handles_quotes_correctly(void) {
    // Test with content containing quotes
    ToolCall tool_call = {
        .id = "test_quotes",
        .name = "file_write",
        .arguments = "{\"file_path\": \"/tmp/test_quotes.txt\", \"content\": \"He said, \\\"Hello, World!\\\"\\nIt's a \\\"test\\\" file.\"}"
    };
    
    ToolResult result = {0};
    int exec_result = execute_file_write_tool_call(&tool_call, &result);
    
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    // Verify content
    char *read_content = NULL;
    file_read_content("/tmp/test_quotes.txt", 0, 0, &read_content);
    
    const char *expected_content = "He said, \"Hello, World!\"\nIt's a \"test\" file.";
    TEST_ASSERT_EQUAL_STRING(expected_content, read_content);
    
    // Cleanup
    free(read_content);
    free(result.tool_call_id);
    free(result.result);
    unlink("/tmp/test_quotes.txt");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_file_write_with_escaped_content);
    RUN_TEST(test_file_append_with_escaped_content);
    RUN_TEST(test_file_write_handles_backslashes_correctly);
    RUN_TEST(test_file_write_handles_quotes_correctly);
    return UNITY_END();
}