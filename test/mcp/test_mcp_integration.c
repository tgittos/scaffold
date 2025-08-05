#include "../unity/unity.h"
#include "../../src/mcp/mcp_client.h"
#include "../../src/core/ralph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
    // Set up function
}

void tearDown(void) {
    // Clean up function
}

void test_mcp_client_basic_initialization(void) {
    MCPClient client;
    
    int result = mcp_client_init(&client);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, client.initialized);
    
    mcp_client_cleanup(&client);
}

void test_mcp_config_loading_with_hosted_server(void) {
    MCPClient client;
    mcp_client_init(&client);
    
    // Create a test config file with hosted server
    const char* config_content = 
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"test_hosted\": {\n"
        "      \"type\": \"sse\",\n"
        "      \"url\": \"https://remote.mcpservers.org/fetch/mcp\",\n"
        "      \"headers\": {\n"
        "        \"Content-Type\": \"application/json\"\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}";
    
    FILE* test_config = fopen("test_mcp_config.json", "w");
    TEST_ASSERT_NOT_NULL(test_config);
    fputs(config_content, test_config);
    fclose(test_config);
    
    // Test loading the config
    int result = mcp_client_load_config(&client, "test_mcp_config.json");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, client.config.server_count);
    TEST_ASSERT_NOT_NULL(client.config.servers);
    TEST_ASSERT_EQUAL_STRING("test_hosted", client.config.servers[0].name);
    TEST_ASSERT_EQUAL(MCP_SERVER_SSE, client.config.servers[0].type);
    TEST_ASSERT_EQUAL_STRING("https://remote.mcpservers.org/fetch/mcp", client.config.servers[0].url);
    
    // Clean up
    unlink("test_mcp_config.json");
    mcp_client_cleanup(&client);
}

void test_ralph_mcp_integration(void) {
    // This test verifies that ralph can initialize with MCP configuration
    // but doesn't try to actually connect to avoid network dependencies in unit tests
    
    // Create a minimal MCP config for ralph
    const char* ralph_config = 
        "{\n"
        "  \"mcpServers\": {\n"
        "    \"test_server\": {\n"
        "      \"type\": \"http\",\n"
        "      \"url\": \"https://example.com/mcp\"\n"
        "    }\n"
        "  }\n"
        "}";
    
    FILE* config_file = fopen("ralph.config.json", "w");
    TEST_ASSERT_NOT_NULL(config_file);
    fputs(ralph_config, config_file);
    fclose(config_file);
    
    RalphSession session;
    
    // Initialize ralph session - this should load MCP config but not connect
    int result = ralph_init_session(&session);
    TEST_ASSERT_EQUAL(0, result);
    
    // Verify MCP client was initialized
    TEST_ASSERT_EQUAL(1, session.mcp_client.initialized);
    
    // Verify config was loaded
    TEST_ASSERT_GREATER_THAN(0, session.mcp_client.config.server_count);
    TEST_ASSERT_NOT_NULL(session.mcp_client.config.servers);
    TEST_ASSERT_EQUAL_STRING("test_server", session.mcp_client.config.servers[0].name);
    
    // Clean up
    ralph_cleanup_session(&session);
    unlink("ralph.config.json");
}

void test_mcp_tool_execution_error_handling(void) {
    MCPClient client;
    mcp_client_init(&client);
    
    // Test tool execution with no servers connected
    ToolCall call = {
        .id = "test_call",
        .name = "mcp_nonexistent_tool", 
        .arguments = "{}"
    };
    
    ToolResult result = {0};
    
    // Should fail gracefully since no servers are connected
    int exec_result = mcp_client_execute_tool(&client, &call, &result);
    TEST_ASSERT_EQUAL(-1, exec_result);
    
    mcp_client_cleanup(&client);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_mcp_client_basic_initialization);
    RUN_TEST(test_mcp_config_loading_with_hosted_server);
    RUN_TEST(test_ralph_mcp_integration);
    RUN_TEST(test_mcp_tool_execution_error_handling);
    
    return UNITY_END();
}