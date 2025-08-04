#include "unity/unity.h"
#include "../src/shell_tool.h"
#include "../src/tools_system.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

void setUp(void) {
    // Set up code before each test
}

void tearDown(void) {
    // Clean up code after each test
}

// Test tool registration
void test_register_shell_tool(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    TEST_ASSERT_EQUAL_INT(0, register_shell_tool(&registry));
    TEST_ASSERT_EQUAL_INT(1, registry.function_count);
    TEST_ASSERT_NOT_NULL(registry.functions);
    TEST_ASSERT_EQUAL_STRING("shell_execute", registry.functions[0].name);
    TEST_ASSERT_NOT_NULL(registry.functions[0].description);
    TEST_ASSERT_EQUAL_INT(4, registry.functions[0].parameter_count);
    
    // Check parameters
    TEST_ASSERT_EQUAL_STRING("command", registry.functions[0].parameters[0].name);
    TEST_ASSERT_EQUAL_INT(1, registry.functions[0].parameters[0].required);
    
    TEST_ASSERT_EQUAL_STRING("working_directory", registry.functions[0].parameters[1].name);
    TEST_ASSERT_EQUAL_INT(0, registry.functions[0].parameters[1].required);
    
    TEST_ASSERT_EQUAL_STRING("timeout_seconds", registry.functions[0].parameters[2].name);
    TEST_ASSERT_EQUAL_INT(0, registry.functions[0].parameters[2].required);
    
    TEST_ASSERT_EQUAL_STRING("capture_stderr", registry.functions[0].parameters[3].name);
    TEST_ASSERT_EQUAL_INT(0, registry.functions[0].parameters[3].required);
    
    cleanup_tool_registry(&registry);
}

// Test command validation
void test_validate_shell_command(void) {
    // Valid commands
    TEST_ASSERT_EQUAL_INT(1, validate_shell_command("ls -la"));
    TEST_ASSERT_EQUAL_INT(1, validate_shell_command("echo 'Hello World'"));
    TEST_ASSERT_EQUAL_INT(1, validate_shell_command("cat /proc/version"));
    TEST_ASSERT_EQUAL_INT(1, validate_shell_command("ps aux"));
    
    // Invalid commands (security risks)
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command("rm -rf /"));
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command("rm -rf /*"));
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command("mkfs.ext4 /dev/sda1"));
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command("dd if=/dev/zero of=/dev/sda"));
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command(":(){ :|:& };:"));
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command("chmod -R 777 /"));
    
    // Edge cases
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command(NULL));
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command(""));
    
    // Command too long
    char long_command[SHELL_MAX_COMMAND_LENGTH + 100];
    memset(long_command, 'a', sizeof(long_command) - 1);
    long_command[sizeof(long_command) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(0, validate_shell_command(long_command));
}

// Test argument parsing
void test_parse_shell_arguments(void) {
    ShellCommandParams params;
    
    // Test basic command parsing
    const char* json1 = "{\"command\": \"ls -la\"}";
    TEST_ASSERT_EQUAL_INT(0, parse_shell_arguments(json1, &params));
    TEST_ASSERT_EQUAL_STRING("ls -la", params.command);
    TEST_ASSERT_NULL(params.working_directory);
    TEST_ASSERT_EQUAL_INT(0, params.timeout_seconds);
    TEST_ASSERT_EQUAL_INT(1, params.capture_stderr);
    cleanup_shell_params(&params);
    
    // Test full parameter parsing
    const char* json2 = "{\"command\": \"echo test\", \"working_directory\": \"/tmp\", \"timeout_seconds\": 30, \"capture_stderr\": false}";
    TEST_ASSERT_EQUAL_INT(0, parse_shell_arguments(json2, &params));
    TEST_ASSERT_EQUAL_STRING("echo test", params.command);
    TEST_ASSERT_EQUAL_STRING("/tmp", params.working_directory);
    TEST_ASSERT_EQUAL_INT(30, params.timeout_seconds);
    TEST_ASSERT_EQUAL_INT(0, params.capture_stderr);
    cleanup_shell_params(&params);
    
    // Test timeout clamping
    const char* json3 = "{\"command\": \"sleep 1\", \"timeout_seconds\": 500}";
    TEST_ASSERT_EQUAL_INT(0, parse_shell_arguments(json3, &params));
    TEST_ASSERT_EQUAL_INT(SHELL_MAX_TIMEOUT_SECONDS, params.timeout_seconds);
    cleanup_shell_params(&params);
    
    // Test invalid JSON
    TEST_ASSERT_EQUAL_INT(-1, parse_shell_arguments(NULL, &params));
    TEST_ASSERT_EQUAL_INT(-1, parse_shell_arguments("{\"invalid\": \"json\"}", &params));
    TEST_ASSERT_EQUAL_INT(-1, parse_shell_arguments("{}", &params));
}

// Test basic shell command execution
void test_execute_shell_command_basic(void) {
    ShellCommandParams params = {0};
    ShellExecutionResult result;
    
    // Test simple echo command
    params.command = strdup("echo 'Hello, World!'");
    params.capture_stderr = 1;
    params.timeout_seconds = 5;
    
    TEST_ASSERT_EQUAL_INT(0, execute_shell_command(&params, &result));
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(result.stderr_output);
    TEST_ASSERT_TRUE(strstr(result.stdout_output, "Hello, World!") != NULL);
    TEST_ASSERT_EQUAL_INT(0, result.exit_code);
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);
    TEST_ASSERT_TRUE(result.execution_time >= 0.0);
    
    cleanup_shell_params(&params);
    cleanup_shell_result(&result);
}

// Test command with non-zero exit code
void test_execute_shell_command_error(void) {
    ShellCommandParams params = {0};
    ShellExecutionResult result;
    
    // Test command that should fail
    params.command = strdup("false");  // Command that always returns 1
    params.capture_stderr = 1;
    params.timeout_seconds = 5;
    
    TEST_ASSERT_EQUAL_INT(0, execute_shell_command(&params, &result));
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(result.stderr_output);
    TEST_ASSERT_EQUAL_INT(1, result.exit_code);
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);
    
    cleanup_shell_params(&params);
    cleanup_shell_result(&result);
}

// Test working directory change
void test_execute_shell_command_workdir(void) {
    ShellCommandParams params = {0};
    ShellExecutionResult result;
    
    // Create a test directory
    const char* test_dir = "/tmp/shell_tool_test";
    mkdir(test_dir, 0755);
    
    // Test command with working directory
    params.command = strdup("pwd");
    params.working_directory = strdup(test_dir);
    params.capture_stderr = 1;
    params.timeout_seconds = 5;
    
    TEST_ASSERT_EQUAL_INT(0, execute_shell_command(&params, &result));
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_TRUE(strstr(result.stdout_output, test_dir) != NULL);
    TEST_ASSERT_EQUAL_INT(0, result.exit_code);
    
    cleanup_shell_params(&params);
    cleanup_shell_result(&result);
    
    // Cleanup test directory
    rmdir(test_dir);
}

// Test timeout functionality
void test_execute_shell_command_timeout(void) {
    ShellCommandParams params = {0};
    ShellExecutionResult result;
    
    // Test command that should timeout
    params.command = strdup("sleep 10");
    params.capture_stderr = 1;
    params.timeout_seconds = 1;  // 1 second timeout
    
    TEST_ASSERT_EQUAL_INT(0, execute_shell_command(&params, &result));
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(result.stderr_output);
    TEST_ASSERT_EQUAL_INT(-1, result.exit_code);
    TEST_ASSERT_EQUAL_INT(1, result.timed_out);
    // The sleep command should be killed by the timeout, so execution time should be around 1 second
    TEST_ASSERT_TRUE(result.execution_time >= 0.9);
    TEST_ASSERT_TRUE(result.execution_time < 5.0);  // Should not exceed timeout by much
    
    cleanup_shell_params(&params);
    cleanup_shell_result(&result);
}

// Test security validation
void test_execute_shell_command_security(void) {
    ShellCommandParams params = {0};
    ShellExecutionResult result;
    
    // Test dangerous command (should be blocked)
    params.command = strdup("rm -rf /");
    params.capture_stderr = 1;
    params.timeout_seconds = 5;
    
    TEST_ASSERT_EQUAL_INT(0, execute_shell_command(&params, &result));
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_TRUE(strstr(result.stdout_output, "security validation") != NULL);
    TEST_ASSERT_EQUAL_INT(-1, result.exit_code);
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);
    
    cleanup_shell_params(&params);
    cleanup_shell_result(&result);
}

// Test JSON result formatting
void test_format_shell_result_json(void) {
    ShellExecutionResult result = {0};
    
    result.stdout_output = strdup("Hello, World!");
    result.stderr_output = strdup("");
    result.exit_code = 0;
    result.execution_time = 0.123;
    result.timed_out = 0;
    
    char* json = format_shell_result_json(&result);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(strstr(json, "\"stdout\": \"Hello, World!\"") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"stderr\": \"\"") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"exit_code\": 0") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"execution_time\": 0.123") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"timed_out\": false") != NULL);
    
    free(json);
    cleanup_shell_result(&result);
}

// Test tool call execution
void test_execute_shell_tool_call(void) {
    ToolCall tool_call = {0};
    ToolResult result = {0};
    
    tool_call.id = strdup("test_call_1");
    tool_call.name = strdup("shell_execute");
    tool_call.arguments = strdup("{\"command\": \"echo test\"}");
    
    TEST_ASSERT_EQUAL_INT(0, execute_shell_tool_call(&tool_call, &result));
    TEST_ASSERT_NOT_NULL(result.tool_call_id);
    TEST_ASSERT_EQUAL_STRING("test_call_1", result.tool_call_id);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "\"stdout\": \"test") != NULL);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    free(tool_call.id);
    free(tool_call.name);
    free(tool_call.arguments);
    free(result.tool_call_id);
    free(result.result);
}

// Test integration with tools system
void test_shell_tool_integration(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);
    
    TEST_ASSERT_EQUAL_INT(0, register_shell_tool(&registry));
    
    // Test tools JSON generation
    char* tools_json = generate_tools_json(&registry);
    TEST_ASSERT_NOT_NULL(tools_json);
    TEST_ASSERT_TRUE(strstr(tools_json, "shell_execute") != NULL);
    TEST_ASSERT_TRUE(strstr(tools_json, "Execute shell commands") != NULL);
    free(tools_json);
    
    // Test tool execution through registry
    ToolCall tool_call = {0};
    ToolResult result = {0};
    
    tool_call.id = strdup("integration_test");
    tool_call.name = strdup("shell_execute");
    tool_call.arguments = strdup("{\"command\": \"echo integration_success\"}");
    
    TEST_ASSERT_EQUAL_INT(0, execute_tool_call(&registry, &tool_call, &result));
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "integration_success") != NULL);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    
    free(tool_call.id);
    free(tool_call.name);
    free(tool_call.arguments);
    free(result.tool_call_id);
    free(result.result);
    
    cleanup_tool_registry(&registry);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_register_shell_tool);
    RUN_TEST(test_validate_shell_command);
    RUN_TEST(test_parse_shell_arguments);
    RUN_TEST(test_execute_shell_command_basic);
    RUN_TEST(test_execute_shell_command_error);
    RUN_TEST(test_execute_shell_command_workdir);
    RUN_TEST(test_execute_shell_command_timeout);
    RUN_TEST(test_execute_shell_command_security);
    RUN_TEST(test_format_shell_result_json);
    RUN_TEST(test_execute_shell_tool_call);
    RUN_TEST(test_shell_tool_integration);
    
    return UNITY_END();
}