#include "unity/unity.h"
#include "../src/mcp/mcp_client.h"
#include "../src/tools/tools_system.h"
#include <stdio.h>
#include <stdlib.h>

void setUp(void) {
    // Set up function
}

void tearDown(void) {
    // Clean up function
}

void test_mcp_client_init(void) {
    MCPClient client;
    
    int result = mcp_client_init(&client);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, client.initialized);
    
    mcp_client_cleanup(&client);
}

void test_mcp_find_config_path(void) {
    char config_path[512];
    
    // Test finding config in current directory (we created ralph.config.json)
    int result = mcp_find_config_path(config_path, sizeof(config_path));
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL_STRING("./ralph.config.json", config_path);
}

void test_mcp_expand_env_vars(void) {
    // Set a test environment variable
    setenv("TEST_VAR", "test_value", 1);
    
    // Test simple variable expansion
    char* result = mcp_expand_env_vars("${TEST_VAR}");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("test_value", result);
    free(result);
    
    // Test with default value (var exists)
    result = mcp_expand_env_vars("${TEST_VAR:-default}");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("test_value", result);
    free(result);
    
    // Test with default value (var doesn't exist)
    result = mcp_expand_env_vars("${NONEXISTENT_VAR:-default_value}");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("default_value", result);
    free(result);
    
    // Test string without variables
    result = mcp_expand_env_vars("no_variables_here");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("no_variables_here", result);
    free(result);
    
    unsetenv("TEST_VAR");
}

void test_mcp_load_config(void) {
    MCPClient client;
    mcp_client_init(&client);
    
    // Test loading our test config
    int result = mcp_client_load_config(&client, "ralph.config.json");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, client.config.server_count);
    TEST_ASSERT_NOT_NULL(client.config.servers);
    TEST_ASSERT_EQUAL_STRING("filesystem", client.config.servers[0].name);
    TEST_ASSERT_EQUAL(MCP_SERVER_STDIO, client.config.servers[0].type);
    TEST_ASSERT_EQUAL_STRING("npx", client.config.servers[0].command);
    
    mcp_client_cleanup(&client);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_mcp_client_init);
    RUN_TEST(test_mcp_find_config_path);
    RUN_TEST(test_mcp_expand_env_vars);
    RUN_TEST(test_mcp_load_config);
    
    return UNITY_END();
}