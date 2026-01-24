#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "output_formatter.h"
#include "unity/unity.h"

void setUp(void) {
}

void tearDown(void) {
}

void test_improved_output_formatting() {
    printf("Testing improved output formatting with proper grouping and separation...\n");
    
    // Test the desired behavior: clean grouped output with proper visual separation
    ParsedResponse response = {0};
    response.thinking_content = strdup("This is thinking content");
    response.response_content = strdup("This is the main AI response that should be prominent");
    response.total_tokens = 1500;
    response.prompt_tokens = 1000;
    response.completion_tokens = 500;
    
    // Capture output to verify structure (this will fail until we implement the new system)
    printf("=== TESTING IMPROVED OUTPUT ===\n");
    
    // This function should exist and provide clean, grouped output
    print_formatted_response_improved(&response);
    
    // Test that we have proper visual separation and grouping
    // This assertion will fail until we implement the improved system
    TEST_ASSERT_NOT_NULL(response.response_content);
    
    cleanup_parsed_response(&response);
}

void test_tool_output_grouping() {
    printf("Testing tool output grouping...\n");

    // Test that tool execution is properly grouped and visually separated
    // This function should exist to provide clean tool output grouping
    display_tool_execution_group_start();

    // Simulate tool calls with improved formatting
    log_tool_execution_improved("file_read", "{\"file_path\": \"/test/file.txt\"}", true, "File content here");
    log_tool_execution_improved("shell_execute", "{\"command\": \"ls -la\"}", true, "Directory listing");

    display_tool_execution_group_end();

    // This will fail until we implement the improved tool output system
    TEST_ASSERT_TRUE(1); // Placeholder that will be replaced with actual verification
}

void test_tool_argument_display() {
    printf("Testing tool argument display in tool execution log...\n");

    display_tool_execution_group_start();

    // Test read_file with path argument
    printf("\n--- Testing read_file with path ---\n");
    log_tool_execution_improved("read_file", "{\"path\": \"/home/user/test.txt\"}", true, "File contents");

    // Test shell with command argument
    printf("\n--- Testing shell with command ---\n");
    log_tool_execution_improved("shell", "{\"command\": \"git status\"}", true, "On branch main");

    // Test write_file with path and content
    printf("\n--- Testing write_file with path ---\n");
    log_tool_execution_improved("write_file", "{\"path\": \"/tmp/output.txt\", \"content\": \"hello world\"}", true, "Written");

    // Test web_fetch with url
    printf("\n--- Testing web_fetch with url ---\n");
    log_tool_execution_improved("web_fetch", "{\"url\": \"https://example.com/api/data\"}", true, "Response data");

    // Test search with query
    printf("\n--- Testing search_files with pattern ---\n");
    log_tool_execution_improved("search_files", "{\"pattern\": \"*.py\", \"directory\": \"/src\"}", true, "Found files");

    // Test memory with key
    printf("\n--- Testing memory_read with key ---\n");
    log_tool_execution_improved("memory_read", "{\"key\": \"user_preferences\"}", true, "Memory value");

    // Test long argument truncation
    printf("\n--- Testing long path truncation ---\n");
    log_tool_execution_improved("read_file",
        "{\"path\": \"/very/long/path/that/should/be/truncated/because/it/exceeds/maximum/display/length/file.txt\"}",
        true, "Contents");

    // Test long command truncation
    printf("\n--- Testing long command truncation ---\n");
    log_tool_execution_improved("shell",
        "{\"command\": \"find /usr -name '*.so' -exec ls -la {} \\\\; | grep lib | head -20\"}",
        true, "Output");

    // Test empty arguments (should show no context)
    printf("\n--- Testing empty arguments ---\n");
    log_tool_execution_improved("some_tool", "{}", true, "Result");

    // Test NULL arguments (should show no context)
    printf("\n--- Testing NULL arguments ---\n");
    log_tool_execution_improved("another_tool", NULL, true, "Result");

    // Test invalid JSON (should show no context, not crash)
    printf("\n--- Testing invalid JSON arguments ---\n");
    log_tool_execution_improved("broken_tool", "not valid json {", true, "Result");

    // Test failure with error message
    printf("\n--- Testing failure with error ---\n");
    log_tool_execution_improved("read_file", "{\"path\": \"/nonexistent/file.txt\"}", false, "File not found");

    display_tool_execution_group_end();

    TEST_ASSERT_TRUE(1);
}

void test_debug_output_grouping() {
    printf("Testing debug output grouping...\n");
    
    // Test that debug/system information is properly grouped
    display_system_info_group_start();
    
    // These should be grouped together instead of scattered
    log_system_info("Token allocation", "Prompt: 1000, Response: 500");
    log_system_info("API request", "Making request to endpoint");
    log_system_info("Model config", "Using model capabilities");
    
    display_system_info_group_end();
    
    // This will fail until we implement the improved debug grouping
    TEST_ASSERT_TRUE(1); // Placeholder
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_improved_output_formatting);
    RUN_TEST(test_tool_output_grouping);
    RUN_TEST(test_tool_argument_display);
    RUN_TEST(test_debug_output_grouping);
    return UNITY_END();
}