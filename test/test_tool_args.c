/**
 * Unit tests for tool_args module
 */

#include "unity/unity.h"
#include "policy/tool_args.h"

#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

/* =============================================================================
 * Helper to create a ToolCall with JSON arguments
 * ========================================================================== */

static ToolCall make_tool_call(const char *name, const char *arguments) {
    ToolCall tc = {0};
    tc.name = (char *)name;
    tc.arguments = (char *)arguments;
    return tc;
}

/* =============================================================================
 * tool_args_get_string Tests
 * ========================================================================== */

void test_tool_args_get_string_returns_value(void) {
    ToolCall tc = make_tool_call("test", "{\"key\": \"value\"}");

    char *result = tool_args_get_string(&tc, "key");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("value", result);
    free(result);
}

void test_tool_args_get_string_missing_key_returns_null(void) {
    ToolCall tc = make_tool_call("test", "{\"other\": \"value\"}");

    char *result = tool_args_get_string(&tc, "missing");
    TEST_ASSERT_NULL(result);
}

void test_tool_args_get_string_null_tool_call_returns_null(void) {
    char *result = tool_args_get_string(NULL, "key");
    TEST_ASSERT_NULL(result);
}

void test_tool_args_get_string_null_key_returns_null(void) {
    ToolCall tc = make_tool_call("test", "{\"key\": \"value\"}");

    char *result = tool_args_get_string(&tc, NULL);
    TEST_ASSERT_NULL(result);
}

void test_tool_args_get_string_null_arguments_returns_null(void) {
    ToolCall tc = make_tool_call("test", NULL);

    char *result = tool_args_get_string(&tc, "key");
    TEST_ASSERT_NULL(result);
}

void test_tool_args_get_string_invalid_json_returns_null(void) {
    ToolCall tc = make_tool_call("test", "not valid json");

    char *result = tool_args_get_string(&tc, "key");
    TEST_ASSERT_NULL(result);
}

void test_tool_args_get_string_non_string_value_returns_null(void) {
    ToolCall tc = make_tool_call("test", "{\"key\": 123}");

    char *result = tool_args_get_string(&tc, "key");
    TEST_ASSERT_NULL(result);
}

/* =============================================================================
 * tool_args_get_command Tests
 * ========================================================================== */

void test_tool_args_get_command_returns_command(void) {
    ToolCall tc = make_tool_call("bash", "{\"command\": \"ls -la\"}");

    char *result = tool_args_get_command(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("ls -la", result);
    free(result);
}

void test_tool_args_get_command_missing_returns_null(void) {
    ToolCall tc = make_tool_call("bash", "{\"other\": \"value\"}");

    char *result = tool_args_get_command(&tc);
    TEST_ASSERT_NULL(result);
}

/* =============================================================================
 * tool_args_get_path Tests
 * ========================================================================== */

void test_tool_args_get_path_with_path_key(void) {
    ToolCall tc = make_tool_call("read", "{\"path\": \"/tmp/file.txt\"}");

    char *result = tool_args_get_path(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/tmp/file.txt", result);
    free(result);
}

void test_tool_args_get_path_with_file_path_key(void) {
    ToolCall tc = make_tool_call("write", "{\"file_path\": \"/home/test.txt\"}");

    char *result = tool_args_get_path(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/home/test.txt", result);
    free(result);
}

void test_tool_args_get_path_with_filepath_key(void) {
    ToolCall tc = make_tool_call("edit", "{\"filepath\": \"/var/log/app.log\"}");

    char *result = tool_args_get_path(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/var/log/app.log", result);
    free(result);
}

void test_tool_args_get_path_with_filename_key(void) {
    ToolCall tc = make_tool_call("open", "{\"filename\": \"document.pdf\"}");

    char *result = tool_args_get_path(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("document.pdf", result);
    free(result);
}

void test_tool_args_get_path_prefers_path_over_file_path(void) {
    ToolCall tc = make_tool_call("test", "{\"path\": \"/first\", \"file_path\": \"/second\"}");

    char *result = tool_args_get_path(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/first", result);
    free(result);
}

void test_tool_args_get_path_missing_returns_null(void) {
    ToolCall tc = make_tool_call("test", "{\"other\": \"value\"}");

    char *result = tool_args_get_path(&tc);
    TEST_ASSERT_NULL(result);
}

/* =============================================================================
 * tool_args_get_url Tests
 * ========================================================================== */

void test_tool_args_get_url_returns_url(void) {
    ToolCall tc = make_tool_call("fetch", "{\"url\": \"https://example.com\"}");

    char *result = tool_args_get_url(&tc);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("https://example.com", result);
    free(result);
}

void test_tool_args_get_url_missing_returns_null(void) {
    ToolCall tc = make_tool_call("fetch", "{\"uri\": \"https://example.com\"}");

    char *result = tool_args_get_url(&tc);
    TEST_ASSERT_NULL(result);
}

/* =============================================================================
 * tool_args_get_int Tests
 * ========================================================================== */

void test_tool_args_get_int_returns_value(void) {
    ToolCall tc = make_tool_call("test", "{\"count\": 42}");
    int value = 0;

    int ret = tool_args_get_int(&tc, "count", &value);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(42, value);
}

void test_tool_args_get_int_negative_value(void) {
    ToolCall tc = make_tool_call("test", "{\"offset\": -10}");
    int value = 0;

    int ret = tool_args_get_int(&tc, "offset", &value);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(-10, value);
}

void test_tool_args_get_int_missing_key_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"other\": 5}");
    int value = 0;

    int ret = tool_args_get_int(&tc, "count", &value);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_tool_args_get_int_non_number_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"count\": \"string\"}");
    int value = 0;

    int ret = tool_args_get_int(&tc, "count", &value);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_tool_args_get_int_null_out_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"count\": 42}");

    int ret = tool_args_get_int(&tc, "count", NULL);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_tool_args_get_int_null_key_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"count\": 42}");
    int value = 0;

    int ret = tool_args_get_int(&tc, NULL, &value);
    TEST_ASSERT_EQUAL(-1, ret);
}

/* =============================================================================
 * tool_args_get_bool Tests
 * ========================================================================== */

void test_tool_args_get_bool_returns_true(void) {
    ToolCall tc = make_tool_call("test", "{\"enabled\": true}");
    int value = 0;

    int ret = tool_args_get_bool(&tc, "enabled", &value);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, value);
}

void test_tool_args_get_bool_returns_false(void) {
    ToolCall tc = make_tool_call("test", "{\"enabled\": false}");
    int value = 1;

    int ret = tool_args_get_bool(&tc, "enabled", &value);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(0, value);
}

void test_tool_args_get_bool_missing_key_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"other\": true}");
    int value = 0;

    int ret = tool_args_get_bool(&tc, "enabled", &value);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_tool_args_get_bool_non_bool_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"enabled\": 1}");
    int value = 0;

    int ret = tool_args_get_bool(&tc, "enabled", &value);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_tool_args_get_bool_null_out_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"enabled\": true}");

    int ret = tool_args_get_bool(&tc, "enabled", NULL);
    TEST_ASSERT_EQUAL(-1, ret);
}

void test_tool_args_get_bool_null_key_returns_error(void) {
    ToolCall tc = make_tool_call("test", "{\"enabled\": true}");
    int value = 0;

    int ret = tool_args_get_bool(&tc, NULL, &value);
    TEST_ASSERT_EQUAL(-1, ret);
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* tool_args_get_string tests */
    RUN_TEST(test_tool_args_get_string_returns_value);
    RUN_TEST(test_tool_args_get_string_missing_key_returns_null);
    RUN_TEST(test_tool_args_get_string_null_tool_call_returns_null);
    RUN_TEST(test_tool_args_get_string_null_key_returns_null);
    RUN_TEST(test_tool_args_get_string_null_arguments_returns_null);
    RUN_TEST(test_tool_args_get_string_invalid_json_returns_null);
    RUN_TEST(test_tool_args_get_string_non_string_value_returns_null);

    /* tool_args_get_command tests */
    RUN_TEST(test_tool_args_get_command_returns_command);
    RUN_TEST(test_tool_args_get_command_missing_returns_null);

    /* tool_args_get_path tests */
    RUN_TEST(test_tool_args_get_path_with_path_key);
    RUN_TEST(test_tool_args_get_path_with_file_path_key);
    RUN_TEST(test_tool_args_get_path_with_filepath_key);
    RUN_TEST(test_tool_args_get_path_with_filename_key);
    RUN_TEST(test_tool_args_get_path_prefers_path_over_file_path);
    RUN_TEST(test_tool_args_get_path_missing_returns_null);

    /* tool_args_get_url tests */
    RUN_TEST(test_tool_args_get_url_returns_url);
    RUN_TEST(test_tool_args_get_url_missing_returns_null);

    /* tool_args_get_int tests */
    RUN_TEST(test_tool_args_get_int_returns_value);
    RUN_TEST(test_tool_args_get_int_negative_value);
    RUN_TEST(test_tool_args_get_int_missing_key_returns_error);
    RUN_TEST(test_tool_args_get_int_non_number_returns_error);
    RUN_TEST(test_tool_args_get_int_null_out_returns_error);
    RUN_TEST(test_tool_args_get_int_null_key_returns_error);

    /* tool_args_get_bool tests */
    RUN_TEST(test_tool_args_get_bool_returns_true);
    RUN_TEST(test_tool_args_get_bool_returns_false);
    RUN_TEST(test_tool_args_get_bool_missing_key_returns_error);
    RUN_TEST(test_tool_args_get_bool_non_bool_returns_error);
    RUN_TEST(test_tool_args_get_bool_null_out_returns_error);
    RUN_TEST(test_tool_args_get_bool_null_key_returns_error);

    return UNITY_END();
}
