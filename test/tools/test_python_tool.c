#include "unity/unity.h"
#include "python_tool.h"
#include "lib/tools/tools_system.h"
#include "util/ralph_home.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
    ralph_home_init(NULL);
}

void tearDown(void) {
    ralph_home_cleanup();
}

// Test tool registration
void test_register_python_tool(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);

    TEST_ASSERT_EQUAL_INT(0, register_python_tool(&registry));
    TEST_ASSERT_EQUAL_INT(1, registry.functions.count);
    TEST_ASSERT_NOT_NULL(registry.functions.data);
    TEST_ASSERT_EQUAL_STRING("python", registry.functions.data[0].name);
    TEST_ASSERT_NOT_NULL(registry.functions.data[0].description);
    TEST_ASSERT_EQUAL_INT(2, registry.functions.data[0].parameter_count);

    // Check parameters
    TEST_ASSERT_EQUAL_STRING("code", registry.functions.data[0].parameters[0].name);
    TEST_ASSERT_EQUAL_STRING("string", registry.functions.data[0].parameters[0].type);
    TEST_ASSERT_EQUAL_INT(1, registry.functions.data[0].parameters[0].required);

    TEST_ASSERT_EQUAL_STRING("timeout", registry.functions.data[0].parameters[1].name);
    TEST_ASSERT_EQUAL_STRING("number", registry.functions.data[0].parameters[1].type);
    TEST_ASSERT_EQUAL_INT(0, registry.functions.data[0].parameters[1].required);

    cleanup_tool_registry(&registry);
}

// Test argument parsing - basic command
void test_parse_python_arguments_basic(void) {
    PythonExecutionParams params;

    const char* json = "{\"code\": \"print('Hello, World!')\"}";
    TEST_ASSERT_EQUAL_INT(0, parse_python_arguments(json, &params));
    TEST_ASSERT_EQUAL_STRING("print('Hello, World!')", params.code);
    TEST_ASSERT_EQUAL_INT(PYTHON_DEFAULT_TIMEOUT, params.timeout_seconds);
    TEST_ASSERT_EQUAL_INT(1, params.capture_stderr);
    cleanup_python_params(&params);
}

// Test argument parsing - with timeout
void test_parse_python_arguments_with_timeout(void) {
    PythonExecutionParams params;

    const char* json = "{\"code\": \"import time; time.sleep(1)\", \"timeout\": 10}";
    TEST_ASSERT_EQUAL_INT(0, parse_python_arguments(json, &params));
    TEST_ASSERT_EQUAL_STRING("import time; time.sleep(1)", params.code);
    TEST_ASSERT_EQUAL_INT(10, params.timeout_seconds);
    cleanup_python_params(&params);
}

// Test argument parsing - timeout clamping
void test_parse_python_arguments_timeout_clamping(void) {
    PythonExecutionParams params;

    // Test maximum timeout clamping
    const char* json = "{\"code\": \"pass\", \"timeout\": 500}";
    TEST_ASSERT_EQUAL_INT(0, parse_python_arguments(json, &params));
    TEST_ASSERT_EQUAL_INT(PYTHON_MAX_TIMEOUT_SECONDS, params.timeout_seconds);
    cleanup_python_params(&params);

    // Test negative timeout defaults
    const char* json2 = "{\"code\": \"pass\", \"timeout\": -5}";
    TEST_ASSERT_EQUAL_INT(0, parse_python_arguments(json2, &params));
    TEST_ASSERT_EQUAL_INT(PYTHON_DEFAULT_TIMEOUT, params.timeout_seconds);
    cleanup_python_params(&params);
}

// Test argument parsing - invalid JSON
void test_parse_python_arguments_invalid(void) {
    PythonExecutionParams params;

    TEST_ASSERT_EQUAL_INT(-1, parse_python_arguments(NULL, &params));
    TEST_ASSERT_EQUAL_INT(-1, parse_python_arguments("{}", &params));
    TEST_ASSERT_EQUAL_INT(-1, parse_python_arguments("{\"invalid\": \"json\"}", &params));
}

// Test argument parsing with escape sequences
void test_parse_python_arguments_escapes(void) {
    PythonExecutionParams params;

    // Test newline escape
    const char* json = "{\"code\": \"print('line1\\nline2')\"}";
    TEST_ASSERT_EQUAL_INT(0, parse_python_arguments(json, &params));
    TEST_ASSERT_TRUE(strchr(params.code, '\n') != NULL);
    cleanup_python_params(&params);

    // Test tab escape
    const char* json2 = "{\"code\": \"print('col1\\tcol2')\"}";
    TEST_ASSERT_EQUAL_INT(0, parse_python_arguments(json2, &params));
    TEST_ASSERT_TRUE(strchr(params.code, '\t') != NULL);
    cleanup_python_params(&params);
}

// Test result JSON formatting
void test_format_python_result_json_success(void) {
    PythonExecutionResult result = {0};

    result.stdout_output = strdup("Hello, World!\n");
    result.stderr_output = strdup("");
    result.exception = NULL;
    result.success = 1;
    result.execution_time = 0.025;
    result.timed_out = 0;

    char* json = format_python_result_json(&result);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(strstr(json, "\"stdout\"") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"stderr\"") != NULL);
    // Check for boolean values (cJSON doesn't add space after colon)
    TEST_ASSERT_TRUE(strstr(json, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"timed_out\":false") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"exception\":null") != NULL);

    free(json);
    cleanup_python_result(&result);
}

// Test result JSON formatting with exception
void test_format_python_result_json_exception(void) {
    PythonExecutionResult result = {0};

    result.stdout_output = strdup("");
    result.stderr_output = strdup("");
    result.exception = strdup("NameError: name 'undefined_var' is not defined");
    result.success = 0;
    result.execution_time = 0.001;
    result.timed_out = 0;

    char* json = format_python_result_json(&result);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(strstr(json, "\"success\":false") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "NameError") != NULL);

    free(json);
    cleanup_python_result(&result);
}

// Test result JSON formatting with timeout
void test_format_python_result_json_timeout(void) {
    PythonExecutionResult result = {0};

    result.stdout_output = strdup("");
    result.stderr_output = strdup("");
    result.exception = strdup("Execution timed out");
    result.success = 0;
    result.execution_time = 30.0;
    result.timed_out = 1;

    char* json = format_python_result_json(&result);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(strstr(json, "\"timed_out\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"success\":false") != NULL);

    free(json);
    cleanup_python_result(&result);
}

// Test cleanup functions
void test_cleanup_python_params(void) {
    PythonExecutionParams params;
    params.code = strdup("print('test')");
    params.timeout_seconds = 30;
    params.capture_stderr = 1;

    cleanup_python_params(&params);

    // After cleanup, code should be NULL and all values zeroed
    TEST_ASSERT_NULL(params.code);
    TEST_ASSERT_EQUAL_INT(0, params.timeout_seconds);
    TEST_ASSERT_EQUAL_INT(0, params.capture_stderr);
}

void test_cleanup_python_result(void) {
    PythonExecutionResult result;
    result.stdout_output = strdup("output");
    result.stderr_output = strdup("errors");
    result.exception = strdup("exception");
    result.success = 1;
    result.execution_time = 1.5;
    result.timed_out = 0;

    cleanup_python_result(&result);

    // After cleanup, all pointers should be NULL and values zeroed
    TEST_ASSERT_NULL(result.stdout_output);
    TEST_ASSERT_NULL(result.stderr_output);
    TEST_ASSERT_NULL(result.exception);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_TRUE(result.execution_time == 0.0);
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);
}

// Test cleanup with NULL pointers (should not crash)
void test_cleanup_null_safety(void) {
    // Test NULL parameter handling (should not crash)
    cleanup_python_params(NULL);
    cleanup_python_result(NULL);

    // Test cleanup of zero-initialized struct
    PythonExecutionParams params = {0};
    cleanup_python_params(&params);
    // Verify struct is still zeroed after cleanup
    TEST_ASSERT_NULL(params.code);
    TEST_ASSERT_EQUAL_INT(0, params.timeout_seconds);
    TEST_ASSERT_EQUAL_INT(0, params.capture_stderr);

    // Test cleanup of zero-initialized result struct
    PythonExecutionResult result = {0};
    cleanup_python_result(&result);
    // Verify struct is still zeroed after cleanup
    TEST_ASSERT_NULL(result.stdout_output);
    TEST_ASSERT_NULL(result.stderr_output);
    TEST_ASSERT_NULL(result.exception);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);
}

// Test interpreter initialization status
void test_python_interpreter_is_initialized(void) {
    // The interpreter should not be initialized at the start of this test
    // (assuming tests run in isolation or interpreter was shut down)
    int initial_status = python_interpreter_is_initialized();

    // If not initialized, test the full init/shutdown cycle
    if (initial_status == 0) {
        TEST_ASSERT_EQUAL_INT(0, python_interpreter_init());
        TEST_ASSERT_EQUAL_INT(1, python_interpreter_is_initialized());

        // Shutdown and verify state returns to uninitialized
        python_interpreter_shutdown();
        TEST_ASSERT_EQUAL_INT(0, python_interpreter_is_initialized());
    } else {
        // Already initialized (from previous test), verify it reports correctly
        TEST_ASSERT_EQUAL_INT(1, initial_status);
    }
}

// Test tool integration with tools system
void test_python_tool_json_generation(void) {
    ToolRegistry registry;
    init_tool_registry(&registry);

    TEST_ASSERT_EQUAL_INT(0, register_python_tool(&registry));

    // Test tools JSON generation
    char* tools_json = generate_tools_json(&registry);
    TEST_ASSERT_NOT_NULL(tools_json);
    TEST_ASSERT_TRUE(strstr(tools_json, "python") != NULL);
    TEST_ASSERT_TRUE(strstr(tools_json, "code") != NULL);
    TEST_ASSERT_TRUE(strstr(tools_json, "timeout") != NULL);
    free(tools_json);

    // Test Anthropic tools JSON generation
    char* anthropic_json = generate_anthropic_tools_json(&registry);
    TEST_ASSERT_NOT_NULL(anthropic_json);
    TEST_ASSERT_TRUE(strstr(anthropic_json, "python") != NULL);
    free(anthropic_json);

    cleanup_tool_registry(&registry);
}

// Test execute_python_code error handling with NULL params
void test_execute_python_code_null_handling(void) {
    PythonExecutionResult result = {0};

    // NULL params should return -1
    TEST_ASSERT_EQUAL_INT(-1, execute_python_code(NULL, &result));

    // NULL result should return -1
    PythonExecutionParams params = {0};
    params.code = strdup("print('test')");
    TEST_ASSERT_EQUAL_INT(-1, execute_python_code(&params, NULL));
    free(params.code);

    // NULL code should set error in result and return 0
    params.code = NULL;
    params.timeout_seconds = 30;
    TEST_ASSERT_EQUAL_INT(0, execute_python_code(&params, &result));
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.exception);
    TEST_ASSERT_TRUE(strstr(result.exception, "No code") != NULL);
    cleanup_python_result(&result);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_register_python_tool);
    RUN_TEST(test_parse_python_arguments_basic);
    RUN_TEST(test_parse_python_arguments_with_timeout);
    RUN_TEST(test_parse_python_arguments_timeout_clamping);
    RUN_TEST(test_parse_python_arguments_invalid);
    RUN_TEST(test_parse_python_arguments_escapes);
    RUN_TEST(test_format_python_result_json_success);
    RUN_TEST(test_format_python_result_json_exception);
    RUN_TEST(test_format_python_result_json_timeout);
    RUN_TEST(test_cleanup_python_params);
    RUN_TEST(test_cleanup_python_result);
    RUN_TEST(test_cleanup_null_safety);
    RUN_TEST(test_python_interpreter_is_initialized);
    RUN_TEST(test_python_tool_json_generation);
    RUN_TEST(test_execute_python_code_null_handling);

    return UNITY_END();
}
