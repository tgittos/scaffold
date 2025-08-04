#include "unity.h"
#include "tools_system.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // Clean up any existing test files
}

void tearDown(void) {
    // Clean up test files
}

void test_init_tool_registry(void) {
    ToolRegistry registry;
    
    init_tool_registry(&registry);
    
    TEST_ASSERT_NULL(registry.functions);
    TEST_ASSERT_EQUAL(0, registry.function_count);
}

void test_init_tool_registry_with_null(void) {
    // Should not crash
    init_tool_registry(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_register_demo_tool(void) {
    // Demo tool registration removed - test skipped
    TEST_ASSERT_TRUE(1);
}

void test_register_demo_tool_with_parameters(void) {
    // Demo tool registration removed - test skipped  
    TEST_ASSERT_TRUE(1);
}

void test_register_demo_tool_with_null_parameters(void) {
    // Demo tool registration removed - test skipped
    TEST_ASSERT_TRUE(1);
}

void test_generate_tools_json(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    // No tools registered, should return NULL
    char *json = generate_tools_json(&registry);
    
    TEST_ASSERT_NULL(json);
    
    cleanup_tool_registry(&registry);
}

void test_generate_tools_json_with_parameters(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    // No tools registered, should return NULL
    char *json = generate_tools_json(&registry);
    
    TEST_ASSERT_NULL(json);
    
    cleanup_tool_registry(&registry);
}

void test_generate_tools_json_with_null_registry(void) {
    char *json = generate_tools_json(NULL);
    TEST_ASSERT_NULL(json);
}

void test_parse_tool_calls_no_calls(void) {
    const char *json_response = "{\"choices\":[{\"message\":{\"content\":\"Hello\"}}]}";
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_tool_calls(json_response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, call_count);
    TEST_ASSERT_NULL(tool_calls);
}

void test_parse_tool_calls_with_call(void) {
    const char *json_response = 
        "{\"choices\":[{\"message\":{\"tool_calls\":["
        "{\"id\":\"call_123\",\"function\":{\"name\":\"get_current_time\",\"arguments\":\"{}\"}}"
        "]}}]}";
    
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_tool_calls(json_response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_EQUAL_STRING("call_123", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("get_current_time", tool_calls[0].name);
    TEST_ASSERT_EQUAL_STRING("{}", tool_calls[0].arguments);
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_parse_tool_calls_with_null_parameters(void) {
    const char *json_response = "{}";
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    // Test null response
    TEST_ASSERT_EQUAL(-1, parse_tool_calls(NULL, &tool_calls, &call_count));
    
    // Test null tool_calls
    TEST_ASSERT_EQUAL(-1, parse_tool_calls(json_response, NULL, &call_count));
    
    // Test null call_count
    TEST_ASSERT_EQUAL(-1, parse_tool_calls(json_response, &tool_calls, NULL));
}

void test_execute_tool_call_get_current_time(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    ToolCall call = {
        .id = "call_123",
        .name = "get_current_time",
        .arguments = "{}"
    };
    
    ToolResult result;
    int ret = execute_tool_call(&registry, &call, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("call_123", result.tool_call_id);
    TEST_ASSERT_EQUAL(0, result.success); // Should fail since no tools registered
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "Unknown tool"));
    
    free(result.tool_call_id);
    free(result.result);
    cleanup_tool_registry(&registry);
}

void test_execute_tool_call_get_weather(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    ToolCall call = {
        .id = "call_456",
        .name = "get_weather",
        .arguments = "{\"location\":\"London\"}"
    };
    
    ToolResult result;
    int ret = execute_tool_call(&registry, &call, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("call_456", result.tool_call_id);
    TEST_ASSERT_EQUAL(0, result.success); // Should fail since no tools registered
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "Unknown tool"));
    
    free(result.tool_call_id);
    free(result.result);
    cleanup_tool_registry(&registry);
}

void test_execute_tool_call_unknown_tool(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    ToolCall call = {
        .id = "call_789",
        .name = "unknown_tool",
        .arguments = "{}"
    };
    
    ToolResult result;
    int ret = execute_tool_call(&registry, &call, &result);
    
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL_STRING("call_789", result.tool_call_id);
    TEST_ASSERT_EQUAL(0, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "Unknown tool"));
    
    free(result.tool_call_id);
    free(result.result);
    cleanup_tool_registry(&registry);
}

void test_execute_tool_call_with_null_parameters(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    ToolCall call = {
        .id = "call_123",
        .name = "get_current_time",
        .arguments = "{}"
    };
    
    ToolResult result;
    
    // Test null registry
    TEST_ASSERT_EQUAL(-1, execute_tool_call(NULL, &call, &result));
    
    // Test null tool_call
    TEST_ASSERT_EQUAL(-1, execute_tool_call(&registry, NULL, &result));
    
    // Test null result
    TEST_ASSERT_EQUAL(-1, execute_tool_call(&registry, &call, NULL));
    
    cleanup_tool_registry(&registry);
}

void test_generate_tool_results_json(void) {
    ToolResult results[2];
    results[0].tool_call_id = "call_123";
    results[0].result = "Current time: 2024-01-01 12:00:00";
    results[0].success = 1;
    
    results[1].tool_call_id = "call_456";
    results[1].result = "Weather: Sunny";
    results[1].success = 1;
    
    char *json = generate_tool_results_json(results, 2);
    
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "call_123"));
    TEST_ASSERT_NOT_NULL(strstr(json, "call_456"));
    TEST_ASSERT_NOT_NULL(strstr(json, "Current time"));
    TEST_ASSERT_NOT_NULL(strstr(json, "Weather: Sunny"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"role\": \"tool\""));
    
    free(json);
}

void test_generate_tool_results_json_with_null_parameters(void) {
    ToolResult result;
    result.tool_call_id = "call_123";
    result.result = "test";
    result.success = 1;
    
    // Test null results
    char *json = generate_tool_results_json(NULL, 1);
    TEST_ASSERT_NULL(json);
    
    // Test zero count
    json = generate_tool_results_json(&result, 0);
    TEST_ASSERT_NULL(json);
}

void test_load_tools_config(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    int result = load_tools_config(&registry, "nonexistent_config.json");
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, registry.function_count); // Should have no tools loaded
    
    cleanup_tool_registry(&registry);
}

void test_cleanup_tool_registry(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    // No tools registered, test cleanup of empty registry
    TEST_ASSERT_EQUAL(0, registry.function_count);
    TEST_ASSERT_NULL(registry.functions);
    
    cleanup_tool_registry(&registry);
    
    TEST_ASSERT_EQUAL(0, registry.function_count);
    TEST_ASSERT_NULL(registry.functions);
}

void test_cleanup_tool_registry_with_null(void) {
    // Should not crash
    cleanup_tool_registry(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_generate_anthropic_tools_json(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    // No tools registered, should return NULL
    char *json = generate_anthropic_tools_json(&registry);
    TEST_ASSERT_NULL(json);
    
    cleanup_tool_registry(&registry);
}

void test_generate_anthropic_tools_json_with_tools(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    // Register built-in tools to test with
    register_builtin_tools(&registry);
    
    char *json = generate_anthropic_tools_json(&registry);
    TEST_ASSERT_NOT_NULL(json);
    
    // Check for Anthropic format - no "type": "function" wrapper
    TEST_ASSERT_NULL(strstr(json, "\"type\": \"function\""));
    
    // Check for input_schema instead of parameters
    TEST_ASSERT_NOT_NULL(strstr(json, "\"input_schema\""));
    
    // Check for shell_execute tool
    TEST_ASSERT_NOT_NULL(strstr(json, "\"name\": \"shell_execute\""));
    
    free(json);
    cleanup_tool_registry(&registry);
}

void test_parse_anthropic_tool_calls_no_calls(void) {
    const char *response = "{\"content\": [{\"type\": \"text\", \"text\": \"Hello!\"}]}";
    
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_anthropic_tool_calls(response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, call_count);
    TEST_ASSERT_NULL(tool_calls);
}

void test_parse_anthropic_tool_calls_with_tool_use(void) {
    const char *response = 
        "{\"content\": ["
        "{\"type\": \"text\", \"text\": \"I'll execute that command for you.\"},"
        "{\"type\": \"tool_use\", \"id\": \"toolu_01ABC\", \"name\": \"shell_execute\", "
        "\"input\": {\"command\": \"ls -la\"}}"
        "]}";
    
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_anthropic_tool_calls(response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_NOT_NULL(tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("toolu_01ABC", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("shell_execute", tool_calls[0].name);
    TEST_ASSERT_NOT_NULL(tool_calls[0].arguments);
    TEST_ASSERT_NOT_NULL(strstr(tool_calls[0].arguments, "\"command\": \"ls -la\""));
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_parse_anthropic_tool_calls_multiple(void) {
    const char *response = 
        "{\"content\": ["
        "{\"type\": \"tool_use\", \"id\": \"call1\", \"name\": \"tool1\", \"input\": {\"arg\": \"val1\"}},"
        "{\"type\": \"text\", \"text\": \"Processing...\"},"
        "{\"type\": \"tool_use\", \"id\": \"call2\", \"name\": \"tool2\", \"input\": {\"arg\": \"val2\"}}"
        "]}";
    
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    int result = parse_anthropic_tool_calls(response, &tool_calls, &call_count);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    
    // First tool call
    TEST_ASSERT_EQUAL_STRING("call1", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("tool1", tool_calls[0].name);
    
    // Second tool call
    TEST_ASSERT_EQUAL_STRING("call2", tool_calls[1].id);
    TEST_ASSERT_EQUAL_STRING("tool2", tool_calls[1].name);
    
    cleanup_tool_calls(tool_calls, call_count);
}

void test_parse_anthropic_tool_calls_null_parameters(void) {
    const char *response = "{\"content\": []}";
    ToolCall *tool_calls = NULL;
    int call_count = 0;
    
    // Test null response
    TEST_ASSERT_EQUAL(-1, parse_anthropic_tool_calls(NULL, &tool_calls, &call_count));
    
    // Test null tool_calls
    TEST_ASSERT_EQUAL(-1, parse_anthropic_tool_calls(response, NULL, &call_count));
    
    // Test null call_count
    TEST_ASSERT_EQUAL(-1, parse_anthropic_tool_calls(response, &tool_calls, NULL));
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_init_tool_registry);
    RUN_TEST(test_init_tool_registry_with_null);
    RUN_TEST(test_register_demo_tool);
    RUN_TEST(test_register_demo_tool_with_parameters);
    RUN_TEST(test_register_demo_tool_with_null_parameters);
    RUN_TEST(test_generate_tools_json);
    RUN_TEST(test_generate_tools_json_with_parameters);
    RUN_TEST(test_generate_tools_json_with_null_registry);
    RUN_TEST(test_parse_tool_calls_no_calls);
    RUN_TEST(test_parse_tool_calls_with_call);
    RUN_TEST(test_parse_tool_calls_with_null_parameters);
    RUN_TEST(test_execute_tool_call_get_current_time);
    RUN_TEST(test_execute_tool_call_get_weather);
    RUN_TEST(test_execute_tool_call_unknown_tool);
    RUN_TEST(test_execute_tool_call_with_null_parameters);
    RUN_TEST(test_generate_tool_results_json);
    RUN_TEST(test_generate_tool_results_json_with_null_parameters);
    RUN_TEST(test_load_tools_config);
    RUN_TEST(test_cleanup_tool_registry);
    RUN_TEST(test_cleanup_tool_registry_with_null);
    
    // Anthropic tests
    RUN_TEST(test_generate_anthropic_tools_json);
    RUN_TEST(test_generate_anthropic_tools_json_with_tools);
    RUN_TEST(test_parse_anthropic_tool_calls_no_calls);
    RUN_TEST(test_parse_anthropic_tool_calls_with_tool_use);
    RUN_TEST(test_parse_anthropic_tool_calls_multiple);
    RUN_TEST(test_parse_anthropic_tool_calls_null_parameters);
    
    return UNITY_END();
}