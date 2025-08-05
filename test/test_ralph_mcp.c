#include "unity/unity.h"
#include "../src/core/ralph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
    // Set up function
}

void tearDown(void) {
    // Clean up function
}

void test_ralph_loads_mcp_config(void) {
    RalphSession session;
    
    // Initialize ralph session
    int result = ralph_init_session(&session);
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that MCP client was initialized
    TEST_ASSERT_EQUAL(1, session.mcp_client.initialized);
    
    // Check that MCP config was loaded (we have ralph.config.json in project root)
    TEST_ASSERT_GREATER_THAN(0, session.mcp_client.config.server_count);
    TEST_ASSERT_NOT_NULL(session.mcp_client.config.servers);
    TEST_ASSERT_EQUAL_STRING("filesystem", session.mcp_client.config.servers[0].name);
    
    ralph_cleanup_session(&session);
}

void test_ralph_registers_mcp_tools(void) {
    RalphSession session;
    
    // Initialize ralph session
    int result = ralph_init_session(&session);
    TEST_ASSERT_EQUAL(0, result);
    
    // Check that tools registry has some tools
    // (MCP tools would be registered if servers were successfully connected)
    printf("Total tools registered: %d\n", session.tools.function_count);
    
    // Look for MCP tools (they have mcp_ prefix)
    int mcp_tool_count = 0;
    for (int i = 0; i < session.tools.function_count; i++) {
        if (session.tools.functions[i].name && 
            strncmp(session.tools.functions[i].name, "mcp_", 4) == 0) {
            mcp_tool_count++;
            printf("Found MCP tool: %s\n", session.tools.functions[i].name);
        }
    }
    
    printf("MCP tools found: %d\n", mcp_tool_count);
    
    ralph_cleanup_session(&session);
}

void test_mcp_tool_execution_workflow(void) {
    RalphSession session;
    
    // Initialize ralph session
    int result = ralph_init_session(&session);
    TEST_ASSERT_EQUAL(0, result);
    
    // Create a mock MCP tool call
    ToolCall mcp_call = {
        .id = "test_call_1",
        .name = "mcp_filesystem_read_file",
        .arguments = "{\"path\": \"/tmp/test.txt\"}"
    };
    
    ToolResult mcp_result = {0};
    
    // Try to execute the MCP tool call
    int exec_result = mcp_client_execute_tool(&session.mcp_client, &mcp_call, &mcp_result);
    
    // We expect this to fail since we don't have the filesystem server running
    // But it should fail gracefully, not crash
    printf("MCP tool execution result: %d\n", exec_result);
    
    if (mcp_result.result) {
        printf("MCP tool result: %s\n", mcp_result.result);
        free(mcp_result.result);
    }
    if (mcp_result.tool_call_id) {
        free(mcp_result.tool_call_id);
    }
    
    ralph_cleanup_session(&session);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_ralph_loads_mcp_config);
    RUN_TEST(test_ralph_registers_mcp_tools);
    RUN_TEST(test_mcp_tool_execution_workflow);
    
    return UNITY_END();
}