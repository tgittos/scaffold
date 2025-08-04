#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/tools_system.h"

// Test enhanced tool feedback functionality
void test_enhanced_tool_feedback() {
    printf("Testing enhanced tool feedback...\n");
    
    // Test with short arguments
    ToolCall short_call = {
        .id = "test1",
        .name = "file_read",
        .arguments = "{\"file_path\": \"/tmp/test.txt\"}"
    };
    
    // Test with long arguments that should be truncated
    char long_content[2000];
    memset(long_content, 'A', sizeof(long_content) - 1);
    long_content[sizeof(long_content) - 1] = '\0';
    
    char long_args[2500];
    snprintf(long_args, sizeof(long_args), 
             "{\"file_path\": \"/tmp/test.txt\", \"content\": \"%s\"}", 
             long_content);
    
    ToolCall long_call = {
        .id = "test2",
        .name = "file_write",
        .arguments = long_args
    };
    
    // Test successful result
    ToolResult success_result = {
        .tool_call_id = "test1",
        .result = "File content successfully read",
        .success = 1
    };
    
    // Test failed result
    ToolResult failure_result = {
        .tool_call_id = "test2", 
        .result = "Error: File not found",
        .success = 0
    };
    
    printf("Enhanced tool feedback test completed.\n");
}

int main() {
    test_enhanced_tool_feedback();
    return 0;
}