#include "unity.h"
#include "../../src/tools/python_tool.h"
#include "../../src/tools/python_tool_files.h"
#include "../../src/tools/tools_system.h"
#include "../../src/utils/ralph_home.h"
#include <string.h>
#include <stdlib.h>

static ToolRegistry registry;

void setUp(void) {
    ralph_home_init(NULL);
    init_tool_registry(&registry);
}

void tearDown(void) {
    cleanup_tool_registry(&registry);
    ralph_home_cleanup();
}

// Test that the Python interpreter initializes correctly
void test_python_interpreter_init_succeeds(void) {
    // Should already be initialized from the test main
    TEST_ASSERT_TRUE(python_interpreter_is_initialized());
}

// Test tool registration and execution through the tool registry
void test_python_tool_through_registry(void) {
    TEST_ASSERT_EQUAL_INT(0, register_python_tool(&registry));
    TEST_ASSERT_EQUAL_INT(1, registry.functions.count);
    TEST_ASSERT_EQUAL_STRING("python", registry.functions.data[0].name);

    ToolCall call = {
        .id = "registry-test-1",
        .name = "python",
        .arguments = "{\"code\": \"print(2 + 2)\"}"
    };
    ToolResult result = {0};

    TEST_ASSERT_EQUAL_INT(0, execute_python_tool_call(&call, &result));
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "\"stdout\":\"4"));

    free(result.tool_call_id);
    free(result.result);
}

// Test basic code execution
void test_python_execute_basic_code(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("print('Hello from Python!')");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "Hello from Python!"));
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test state persistence across calls
void test_python_state_persists(void) {
    // First call: define a variable
    PythonExecutionParams params1 = {0};
    params1.code = strdup("my_persistent_var = 42");
    params1.timeout_seconds = 30;

    PythonExecutionResult result1 = {0};
    int ret1 = execute_python_code(&params1, &result1);

    TEST_ASSERT_EQUAL_INT(0, ret1);
    TEST_ASSERT_EQUAL_INT(1, result1.success);

    cleanup_python_params(&params1);
    cleanup_python_result(&result1);

    // Second call: use the variable
    PythonExecutionParams params2 = {0};
    params2.code = strdup("print(my_persistent_var * 2)");
    params2.timeout_seconds = 30;

    PythonExecutionResult result2 = {0};
    int ret2 = execute_python_code(&params2, &result2);

    TEST_ASSERT_EQUAL_INT(0, ret2);
    TEST_ASSERT_EQUAL_INT(1, result2.success);
    TEST_ASSERT_NOT_NULL(result2.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result2.stdout_output, "84"));

    cleanup_python_params(&params2);
    cleanup_python_result(&result2);
}

// Test function persistence
void test_python_function_persists(void) {
    // First call: define a function
    PythonExecutionParams params1 = {0};
    params1.code = strdup("def double(x):\n    return x * 2");
    params1.timeout_seconds = 30;

    PythonExecutionResult result1 = {0};
    int ret1 = execute_python_code(&params1, &result1);

    TEST_ASSERT_EQUAL_INT(0, ret1);
    TEST_ASSERT_EQUAL_INT(1, result1.success);

    cleanup_python_params(&params1);
    cleanup_python_result(&result1);

    // Second call: use the function
    PythonExecutionParams params2 = {0};
    params2.code = strdup("print(double(21))");
    params2.timeout_seconds = 30;

    PythonExecutionResult result2 = {0};
    int ret2 = execute_python_code(&params2, &result2);

    TEST_ASSERT_EQUAL_INT(0, ret2);
    TEST_ASSERT_EQUAL_INT(1, result2.success);
    TEST_ASSERT_NOT_NULL(result2.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result2.stdout_output, "42"));

    cleanup_python_params(&params2);
    cleanup_python_result(&result2);
}

// Test exception handling
void test_python_exception_handling(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("raise ValueError('Test exception message')");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.exception);
    TEST_ASSERT_NOT_NULL(strstr(result.exception, "ValueError"));
    TEST_ASSERT_NOT_NULL(strstr(result.exception, "Test exception message"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that interpreter state is preserved after exception
void test_python_state_after_exception(void) {
    // Define a variable
    PythonExecutionParams params1 = {0};
    params1.code = strdup("recovery_var = 123");
    params1.timeout_seconds = 30;

    PythonExecutionResult result1 = {0};
    execute_python_code(&params1, &result1);
    cleanup_python_params(&params1);
    cleanup_python_result(&result1);

    // Cause an exception
    PythonExecutionParams params2 = {0};
    params2.code = strdup("1/0");
    params2.timeout_seconds = 30;

    PythonExecutionResult result2 = {0};
    execute_python_code(&params2, &result2);
    TEST_ASSERT_EQUAL_INT(0, result2.success);
    cleanup_python_params(&params2);
    cleanup_python_result(&result2);

    // Variable should still exist
    PythonExecutionParams params3 = {0};
    params3.code = strdup("print(recovery_var)");
    params3.timeout_seconds = 30;

    PythonExecutionResult result3 = {0};
    execute_python_code(&params3, &result3);

    TEST_ASSERT_EQUAL_INT(1, result3.success);
    TEST_ASSERT_NOT_NULL(result3.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result3.stdout_output, "123"));

    cleanup_python_params(&params3);
    cleanup_python_result(&result3);
}

// Test timeout (with short timeout)
void test_python_timeout(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("import time; time.sleep(10)");
    params.timeout_seconds = 1;  // 1 second timeout

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_EQUAL_INT(1, result.timed_out);

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test stdlib import (json module)
void test_python_stdlib_json(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("import json; print(json.dumps({'key': 'value'}))");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success, "json import should succeed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "key"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test stdlib import (math module)
void test_python_stdlib_math(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("import math; print(int(math.sqrt(144)))");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "12"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that shell tool exit_code propagates to result->success
void test_shell_tool_exit_code_propagates(void) {
    // Initialize tool files
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    // Create a registry and register tools
    ToolRegistry shell_registry;
    init_tool_registry(&shell_registry);
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&shell_registry));

    // Test 1: Command that exits 0 should have success=1
    ToolCall call_success = {
        .id = "shell-exit-0",
        .name = "shell",
        .arguments = "{\"command\": \"true\"}"
    };
    ToolResult result_success = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call_success, &result_success));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result_success.success,
        "Shell command 'true' (exit 0) should report success=1");
    TEST_ASSERT_NOT_NULL(result_success.result);
    TEST_ASSERT_NOT_NULL(strstr(result_success.result, "\"exit_code\": 0"));
    free(result_success.tool_call_id);
    free(result_success.result);

    // Test 2: Command that exits non-zero should have success=0
    ToolCall call_fail = {
        .id = "shell-exit-1",
        .name = "shell",
        .arguments = "{\"command\": \"false\"}"
    };
    ToolResult result_fail = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call_fail, &result_fail));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result_fail.success,
        "Shell command 'false' (exit 1) should report success=0");
    TEST_ASSERT_NOT_NULL(result_fail.result);
    TEST_ASSERT_NOT_NULL(strstr(result_fail.result, "\"exit_code\": 1"));
    free(result_fail.tool_call_id);
    free(result_fail.result);

    // Test 3: Command with specific exit code
    ToolCall call_exit42 = {
        .id = "shell-exit-42",
        .name = "shell",
        .arguments = "{\"command\": \"exit 42\"}"
    };
    ToolResult result_exit42 = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call_exit42, &result_exit42));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result_exit42.success,
        "Shell command 'exit 42' should report success=0");
    TEST_ASSERT_NOT_NULL(result_exit42.result);
    TEST_ASSERT_NOT_NULL(strstr(result_exit42.result, "\"exit_code\": 42"));
    free(result_exit42.tool_call_id);
    free(result_exit42.result);

    cleanup_tool_registry(&shell_registry);
    python_cleanup_tool_files();
}

// Test that docstring Args section is parsed correctly for tool descriptions
void test_python_tool_docstring_parsing(void) {
    // Initialize tool files
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());

    // Load Python tool files
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    // Create a registry and register tools
    ToolRegistry file_registry;
    init_tool_registry(&file_registry);
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&file_registry));

    // Find the apply_delta tool
    int found_apply_delta = 0;
    for (size_t i = 0; i < file_registry.functions.count; i++) {
        if (strcmp(file_registry.functions.data[i].name, "apply_delta") == 0) {
            found_apply_delta = 1;

            // Check that 'operations' parameter has a real description
            // (not just "operations" which was the old broken behavior)
            for (int j = 0; j < file_registry.functions.data[i].parameter_count; j++) {
                if (strcmp(file_registry.functions.data[i].parameters[j].name, "operations") == 0) {
                    const char *desc = file_registry.functions.data[i].parameters[j].description;
                    TEST_ASSERT_NOT_NULL(desc);
                    // Description should contain details about the operations format
                    TEST_ASSERT_NOT_NULL_MESSAGE(
                        strstr(desc, "delta"),
                        "operations description should mention 'delta'"
                    );
                    // Should mention the operation structure details
                    TEST_ASSERT_NOT_NULL_MESSAGE(
                        strstr(desc, "type"),
                        "operations description should mention 'type' field"
                    );
                    break;
                }
            }
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(found_apply_delta, "apply_delta tool should be registered");

    cleanup_tool_registry(&file_registry);
    python_cleanup_tool_files();
}

int main(void) {
    // Initialize ralph home before Python interpreter (needed for tool files path)
    ralph_home_init(NULL);

    // Initialize interpreter once for all tests
    if (python_interpreter_init() != 0) {
        fprintf(stderr, "Failed to initialize Python interpreter\n");
        ralph_home_cleanup();
        return 1;
    }

    UNITY_BEGIN();

    RUN_TEST(test_python_interpreter_init_succeeds);
    RUN_TEST(test_python_tool_through_registry);
    RUN_TEST(test_python_execute_basic_code);
    RUN_TEST(test_python_state_persists);
    RUN_TEST(test_python_function_persists);
    RUN_TEST(test_python_exception_handling);
    RUN_TEST(test_python_state_after_exception);
    RUN_TEST(test_python_timeout);
    RUN_TEST(test_python_stdlib_json);
    RUN_TEST(test_python_stdlib_math);
    RUN_TEST(test_shell_tool_exit_code_propagates);
    RUN_TEST(test_python_tool_docstring_parsing);

    int result = UNITY_END();

    python_interpreter_shutdown();
    ralph_home_cleanup();

    return result;
}
