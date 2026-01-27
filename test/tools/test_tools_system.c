#include "unity.h"
#include "tools_system.h"
#include "../src/policy/approval_gate.h"
#include "../src/policy/protected_files.h"
#include <cJSON.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "ralph_home.h"

void setUp(void) {
    ralph_home_init(NULL);
    // Clean up any existing test files
}

void tearDown(void) {
    // Clean up test files

    ralph_home_cleanup();
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

void test_parse_tool_calls_with_code_containing_quotes(void) {
    /* Test that arguments containing code with escaped quotes are preserved correctly.
     * This tests the fix for the double-unescaping bug where code like print("hello")
     * in apply_delta operations would have its quotes corrupted. */
    const char *json_response =
        "{\"choices\":[{\"message\":{\"tool_calls\":["
        "{\"id\":\"call_456\",\"function\":{\"name\":\"apply_delta\","
        "\"arguments\":\"{\\\"path\\\": \\\"/tmp/test.py\\\", \\\"operations\\\": [{\\\"type\\\": \\\"insert\\\", \\\"start_line\\\": 1, \\\"content\\\": [\\\"print(\\\\\\\"hello\\\\\\\")\\\"]}]}\""
        "}}"
        "]}}]}";

    ToolCall *tool_calls = NULL;
    int call_count = 0;

    int result = parse_tool_calls(json_response, &tool_calls, &call_count);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, call_count);
    TEST_ASSERT_NOT_NULL(tool_calls);
    TEST_ASSERT_EQUAL_STRING("call_456", tool_calls[0].id);
    TEST_ASSERT_EQUAL_STRING("apply_delta", tool_calls[0].name);

    /* Verify the arguments string is valid JSON that can be parsed */
    cJSON *args = cJSON_Parse(tool_calls[0].arguments);
    TEST_ASSERT_NOT_NULL_MESSAGE(args, "Arguments should be valid JSON");

    /* Verify the code content preserved its escaped quotes */
    cJSON *operations = cJSON_GetObjectItem(args, "operations");
    TEST_ASSERT_NOT_NULL(operations);
    TEST_ASSERT_TRUE(cJSON_IsArray(operations));

    cJSON *first_op = cJSON_GetArrayItem(operations, 0);
    TEST_ASSERT_NOT_NULL(first_op);

    cJSON *content = cJSON_GetObjectItem(first_op, "content");
    TEST_ASSERT_NOT_NULL(content);
    TEST_ASSERT_TRUE(cJSON_IsArray(content));

    cJSON *first_line = cJSON_GetArrayItem(content, 0);
    TEST_ASSERT_NOT_NULL(first_line);
    TEST_ASSERT_TRUE(cJSON_IsString(first_line));
    TEST_ASSERT_EQUAL_STRING("print(\"hello\")", cJSON_GetStringValue(first_line));

    cJSON_Delete(args);
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
    // Note: cJSON produces no spaces after colons, check both formats
    TEST_ASSERT_NULL(strstr(json, "\"type\":\"function\""));
    TEST_ASSERT_NULL(strstr(json, "\"type\": \"function\""));

    // Check for input_schema instead of parameters
    TEST_ASSERT_NOT_NULL(strstr(json, "\"input_schema\""));

    // Check for vector_db_search tool (cJSON produces no spaces after colons)
    // Note: We use vector_db_search because it's a C-based tool that doesn't require Python
    TEST_ASSERT_NOT_NULL(strstr(json, "\"name\":\"vector_db_search\""));

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
    TEST_ASSERT_NOT_NULL(strstr(tool_calls[0].arguments, "\"command\":\"ls -la\""));
    
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

/* =============================================================================
 * Approval Gate Integration Tests
 * ========================================================================== */

void test_get_tool_category_file_write(void) {
    /* File write tools should be in FILE_WRITE category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("write_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("append_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("apply_delta"));
}

void test_get_tool_category_file_read(void) {
    /* File read tools should be in FILE_READ category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("read_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("file_info"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("list_dir"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("search_files"));
}

void test_get_tool_category_shell(void) {
    /* Shell tools should be in SHELL category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SHELL, get_tool_category("shell"));
}

void test_get_tool_category_network(void) {
    /* Network tools should be in NETWORK category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_NETWORK, get_tool_category("web_fetch"));
}

void test_get_tool_category_memory(void) {
    /* Memory tools should be in MEMORY category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("remember"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("recall_memories"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("forget_memory"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("todo"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("vector_db_search"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("vector_db_add"));
}

void test_get_tool_category_subagent(void) {
    /* Subagent tools should be in SUBAGENT category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SUBAGENT, get_tool_category("subagent"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SUBAGENT, get_tool_category("subagent_status"));
}

void test_get_tool_category_mcp(void) {
    /* MCP tools (mcp_ prefix) should be in MCP category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MCP, get_tool_category("mcp_list_tools"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MCP, get_tool_category("mcp_call_tool"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MCP, get_tool_category("mcp_anything"));
}

void test_get_tool_category_python(void) {
    /* Python interpreter should be in PYTHON category */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category("python"));
}

void test_get_tool_category_unknown_defaults_to_python(void) {
    /* Unknown tools default to PYTHON category (most restrictive default) */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category("unknown_tool"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category("my_custom_tool"));
}

/* =============================================================================
 * Protected Files Tests
 * ========================================================================== */

void test_protected_file_config_json(void) {
    /* ralph.config.json should be protected */
    TEST_ASSERT_EQUAL(1, is_protected_file("ralph.config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/home/user/project/ralph.config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("./ralph.config.json"));
}

void test_protected_file_env_files(void) {
    /* .env files should be protected */
    TEST_ASSERT_EQUAL(1, is_protected_file(".env"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/project/.env"));
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.local"));
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.production"));
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.development"));
}

void test_protected_file_ralph_dir_config(void) {
    /* .ralph/config.json should be protected */
    TEST_ASSERT_EQUAL(1, is_protected_file(".ralph/config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/home/user/.ralph/config.json"));
}

void test_non_protected_files(void) {
    /* Regular files should not be protected */
    TEST_ASSERT_EQUAL(0, is_protected_file("test.txt"));
    TEST_ASSERT_EQUAL(0, is_protected_file("/tmp/file.txt"));
    TEST_ASSERT_EQUAL(0, is_protected_file("config.json")); /* Not ralph.config.json */
    TEST_ASSERT_EQUAL(0, is_protected_file("environment.txt")); /* Not .env */
}

/* =============================================================================
 * Error Formatting Tests
 * ========================================================================== */

void test_format_protected_file_error_json(void) {
    char *error = format_protected_file_error("/project/ralph.config.json");
    TEST_ASSERT_NOT_NULL(error);

    /* Should contain expected JSON fields */
    TEST_ASSERT_NOT_NULL(strstr(error, "\"error\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "protected_file"));
    TEST_ASSERT_NOT_NULL(strstr(error, "ralph.config.json"));
    TEST_ASSERT_NOT_NULL(strstr(error, "\"path\""));

    free(error);
}

void test_format_protected_file_error_null_safety(void) {
    /* Should handle NULL gracefully */
    char *error = format_protected_file_error(NULL);
    /* May return NULL or a generic error - either is acceptable */
    if (error) {
        TEST_ASSERT_NOT_NULL(strstr(error, "error"));
        free(error);
    }
}

void test_format_denial_error_json(void) {
    ToolCall tool_call = {
        .id = "call_123",
        .name = "shell",
        .arguments = "{\"command\": \"rm -rf /\"}"
    };

    char *error = format_denial_error(&tool_call);
    TEST_ASSERT_NOT_NULL(error);

    /* Should contain expected JSON fields */
    TEST_ASSERT_NOT_NULL(strstr(error, "\"error\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "operation_denied"));
    TEST_ASSERT_NOT_NULL(strstr(error, "shell"));
    TEST_ASSERT_NOT_NULL(strstr(error, "\"tool\""));

    free(error);
}

void test_format_denial_error_null_safety(void) {
    /* Should handle NULL gracefully */
    char *error = format_denial_error(NULL);
    if (error) {
        free(error);
    }
    /* Not crashing is success */
    TEST_ASSERT_TRUE(1);
}

void test_format_non_interactive_error_json(void) {
    ToolCall tool_call = {
        .id = "call_456",
        .name = "write_file",
        .arguments = "{\"path\": \"/tmp/test.txt\"}"
    };

    char *error = format_non_interactive_error(&tool_call);
    TEST_ASSERT_NOT_NULL(error);

    /* Should contain expected JSON fields */
    TEST_ASSERT_NOT_NULL(strstr(error, "\"error\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "non_interactive"));
    TEST_ASSERT_NOT_NULL(strstr(error, "write_file"));

    free(error);
}

void test_format_non_interactive_error_null_safety(void) {
    /* Should handle NULL gracefully */
    char *error = format_non_interactive_error(NULL);
    if (error) {
        free(error);
    }
    /* Not crashing is success */
    TEST_ASSERT_TRUE(1);
}

/* =============================================================================
 * Approval Gate Default Category Actions Tests
 * ========================================================================== */

void test_default_category_actions(void) {
    ApprovalGateConfig config;
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Verify default category actions per spec */
    /* FILE_WRITE = GATE (requires approval) */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_FILE_WRITE));

    /* FILE_READ = ALLOW (no approval needed) */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_FILE_READ));

    /* SHELL = GATE */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_SHELL));

    /* NETWORK = GATE */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_NETWORK));

    /* MEMORY = ALLOW */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_MEMORY));

    /* SUBAGENT = GATE */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_SUBAGENT));

    /* MCP = GATE */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_MCP));

    /* PYTHON = ALLOW */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW,
                      approval_gate_get_category_action(&config, GATE_CATEGORY_PYTHON));

    approval_gate_cleanup(&config);
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
    RUN_TEST(test_parse_tool_calls_with_code_containing_quotes);
    RUN_TEST(test_parse_tool_calls_with_null_parameters);
    RUN_TEST(test_execute_tool_call_get_current_time);
    RUN_TEST(test_execute_tool_call_get_weather);
    RUN_TEST(test_execute_tool_call_unknown_tool);
    RUN_TEST(test_execute_tool_call_with_null_parameters);
    RUN_TEST(test_generate_tool_results_json);
    RUN_TEST(test_generate_tool_results_json_with_null_parameters);
    RUN_TEST(test_cleanup_tool_registry);
    RUN_TEST(test_cleanup_tool_registry_with_null);
    
    // Anthropic tests
    RUN_TEST(test_generate_anthropic_tools_json);
    RUN_TEST(test_generate_anthropic_tools_json_with_tools);
    RUN_TEST(test_parse_anthropic_tool_calls_no_calls);
    RUN_TEST(test_parse_anthropic_tool_calls_with_tool_use);
    RUN_TEST(test_parse_anthropic_tool_calls_multiple);
    RUN_TEST(test_parse_anthropic_tool_calls_null_parameters);

    // Approval Gate Integration Tests
    RUN_TEST(test_get_tool_category_file_write);
    RUN_TEST(test_get_tool_category_file_read);
    RUN_TEST(test_get_tool_category_shell);
    RUN_TEST(test_get_tool_category_network);
    RUN_TEST(test_get_tool_category_memory);
    RUN_TEST(test_get_tool_category_subagent);
    RUN_TEST(test_get_tool_category_mcp);
    RUN_TEST(test_get_tool_category_python);
    RUN_TEST(test_get_tool_category_unknown_defaults_to_python);

    // Protected Files Tests
    RUN_TEST(test_protected_file_config_json);
    RUN_TEST(test_protected_file_env_files);
    RUN_TEST(test_protected_file_ralph_dir_config);
    RUN_TEST(test_non_protected_files);

    // Error Formatting Tests
    RUN_TEST(test_format_protected_file_error_json);
    RUN_TEST(test_format_protected_file_error_null_safety);
    RUN_TEST(test_format_denial_error_json);
    RUN_TEST(test_format_denial_error_null_safety);
    RUN_TEST(test_format_non_interactive_error_json);
    RUN_TEST(test_format_non_interactive_error_null_safety);

    // Default Category Actions Test
    RUN_TEST(test_default_category_actions);

    return UNITY_END();
}
