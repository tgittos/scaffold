#include "unity.h"
#include "json_output.h"
#include "tools_system.h"
#include "streaming.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Buffer to capture stdout
static char captured_output[8192];
static int original_stdout = -1;
static int pipe_fds[2] = {-1, -1};

// Helper to start capturing stdout
static void start_stdout_capture(void) {
    memset(captured_output, 0, sizeof(captured_output));

    if (pipe(pipe_fds) == -1) {
        TEST_FAIL_MESSAGE("Failed to create pipe for stdout capture");
        return;
    }

    original_stdout = dup(STDOUT_FILENO);
    dup2(pipe_fds[1], STDOUT_FILENO);
}

// Helper to end capturing and read output
static ssize_t end_stdout_capture(void) {
    fflush(stdout);

    // Restore stdout FIRST - this closes the pipe write end via STDOUT_FILENO
    // which is necessary to unblock read() below
    if (original_stdout != -1) {
        dup2(original_stdout, STDOUT_FILENO);
        close(original_stdout);
        original_stdout = -1;
    }

    // Now close our reference to the write end (may already be closed by dup2 above)
    if (pipe_fds[1] != -1) {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }

    // Read captured output - now safe since write end is fully closed
    ssize_t bytes_read = 0;
    if (pipe_fds[0] != -1) {
        bytes_read = read(pipe_fds[0], captured_output, sizeof(captured_output) - 1);
        if (bytes_read > 0) {
            captured_output[bytes_read] = '\0';
        }
        close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }

    return bytes_read;
}

void setUp(void) {
    // Nothing to do - each test manages its own capture
}

void tearDown(void) {
    // Clean up any leftover state if a test failed early
    if (pipe_fds[1] != -1) {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }
    if (pipe_fds[0] != -1) {
        close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }
    if (original_stdout != -1) {
        dup2(original_stdout, STDOUT_FILENO);
        close(original_stdout);
        original_stdout = -1;
    }
}

// =============================================================================
// Tests for json_output_assistant_text
// =============================================================================

void test_json_output_assistant_text_basic(void) {
    start_stdout_capture();
    json_output_assistant_text("Hello world", 100, 50);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"assistant\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"text\":\"Hello world\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"input_tokens\":100"));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"output_tokens\":50"));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\n"));
}

void test_json_output_assistant_text_null(void) {
    start_stdout_capture();
    json_output_assistant_text(NULL, 100, 50);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

void test_json_output_assistant_text_special_chars(void) {
    start_stdout_capture();
    json_output_assistant_text("Hello \"world\" with\nnewlines", 10, 5);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"assistant\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"text\":"));
}

// =============================================================================
// Tests for json_output_tool_result
// =============================================================================

void test_json_output_tool_result_success(void) {
    start_stdout_capture();
    json_output_tool_result("call_123", "Tool output here", false);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"user\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"tool_result\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"tool_use_id\":\"call_123\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"content\":\"Tool output here\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"is_error\":false"));
}

void test_json_output_tool_result_error(void) {
    start_stdout_capture();
    json_output_tool_result("call_456", "Error message", true);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"is_error\":true"));
}

void test_json_output_tool_result_null_id(void) {
    start_stdout_capture();
    json_output_tool_result(NULL, "Content", false);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

void test_json_output_tool_result_null_content(void) {
    start_stdout_capture();
    json_output_tool_result("call_789", NULL, false);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"content\":\"\""));
}

// =============================================================================
// Tests for json_output_system and json_output_error
// =============================================================================

void test_json_output_system_with_subtype(void) {
    start_stdout_capture();
    json_output_system("warning", "This is a warning");
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"system\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"subtype\":\"warning\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"message\":\"This is a warning\""));
}

void test_json_output_system_null_subtype(void) {
    start_stdout_capture();
    json_output_system(NULL, "Message without subtype");
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"system\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"message\":\"Message without subtype\""));
    // Should NOT have subtype field when NULL
    TEST_ASSERT_NULL(strstr(captured_output, "\"subtype\""));
}

void test_json_output_error(void) {
    start_stdout_capture();
    json_output_error("Something went wrong");
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"system\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"subtype\":\"error\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"message\":\"Something went wrong\""));
}

void test_json_output_error_null(void) {
    start_stdout_capture();
    json_output_error(NULL);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"message\":\"Unknown error\""));
}

// =============================================================================
// Tests for json_output_result
// =============================================================================

void test_json_output_result(void) {
    start_stdout_capture();
    json_output_result("Final result text");
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"result\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"result\":\"Final result text\""));
}

void test_json_output_result_null(void) {
    start_stdout_capture();
    json_output_result(NULL);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

// =============================================================================
// Tests for json_output_assistant_tool_calls_buffered
// =============================================================================

void test_json_output_tool_calls_single(void) {
    ToolCall tool_calls[1];
    tool_calls[0].id = "call_abc";
    tool_calls[0].name = "shell_execute";
    tool_calls[0].arguments = "{\"command\":\"ls\"}";

    start_stdout_capture();
    json_output_assistant_tool_calls_buffered(tool_calls, 1, 200, 100);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"assistant\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"tool_use\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"id\":\"call_abc\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"name\":\"shell_execute\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"input\":{"));
}

void test_json_output_tool_calls_multiple(void) {
    ToolCall tool_calls[2];
    tool_calls[0].id = "call_1";
    tool_calls[0].name = "tool_a";
    tool_calls[0].arguments = "{\"arg\":1}";
    tool_calls[1].id = "call_2";
    tool_calls[1].name = "tool_b";
    tool_calls[1].arguments = "{\"arg\":2}";

    start_stdout_capture();
    json_output_assistant_tool_calls_buffered(tool_calls, 2, 300, 150);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"id\":\"call_1\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"id\":\"call_2\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"name\":\"tool_a\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"name\":\"tool_b\""));
}

void test_json_output_tool_calls_null_array(void) {
    start_stdout_capture();
    json_output_assistant_tool_calls_buffered(NULL, 1, 100, 50);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

void test_json_output_tool_calls_zero_count(void) {
    ToolCall tool_calls[1];
    tool_calls[0].id = "call_xyz";
    tool_calls[0].name = "test";
    tool_calls[0].arguments = "{}";

    start_stdout_capture();
    json_output_assistant_tool_calls_buffered(tool_calls, 0, 100, 50);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

// =============================================================================
// Tests for json_output_assistant_tool_calls (streaming variant)
// =============================================================================

void test_json_output_streaming_tool_calls_single(void) {
    StreamingToolUse tool_uses[1];
    tool_uses[0].id = "stream_call_1";
    tool_uses[0].name = "streaming_tool";
    tool_uses[0].arguments_json = "{\"stream\":true}";

    start_stdout_capture();
    json_output_assistant_tool_calls(tool_uses, 1, 150, 75);
    end_stdout_capture();

    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"assistant\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"type\":\"tool_use\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"id\":\"stream_call_1\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"name\":\"streaming_tool\""));
    TEST_ASSERT_NOT_NULL(strstr(captured_output, "\"input\":{"));
}

void test_json_output_streaming_tool_calls_null_array(void) {
    start_stdout_capture();
    json_output_assistant_tool_calls(NULL, 1, 100, 50);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

void test_json_output_streaming_tool_calls_zero_count(void) {
    StreamingToolUse tool_uses[1];
    tool_uses[0].id = "test_id";
    tool_uses[0].name = "test";
    tool_uses[0].arguments_json = "{}";

    start_stdout_capture();
    json_output_assistant_tool_calls(tool_uses, 0, 100, 50);
    ssize_t bytes_read = end_stdout_capture();

    TEST_ASSERT_EQUAL(0, bytes_read);
}

// =============================================================================
// Test Runner
// =============================================================================

int main(void) {
    UNITY_BEGIN();

    // json_output_assistant_text tests
    RUN_TEST(test_json_output_assistant_text_basic);
    RUN_TEST(test_json_output_assistant_text_null);
    RUN_TEST(test_json_output_assistant_text_special_chars);

    // json_output_tool_result tests
    RUN_TEST(test_json_output_tool_result_success);
    RUN_TEST(test_json_output_tool_result_error);
    RUN_TEST(test_json_output_tool_result_null_id);
    RUN_TEST(test_json_output_tool_result_null_content);

    // json_output_system and json_output_error tests
    RUN_TEST(test_json_output_system_with_subtype);
    RUN_TEST(test_json_output_system_null_subtype);
    RUN_TEST(test_json_output_error);
    RUN_TEST(test_json_output_error_null);

    // json_output_result tests
    RUN_TEST(test_json_output_result);
    RUN_TEST(test_json_output_result_null);

    // json_output_assistant_tool_calls_buffered tests
    RUN_TEST(test_json_output_tool_calls_single);
    RUN_TEST(test_json_output_tool_calls_multiple);
    RUN_TEST(test_json_output_tool_calls_null_array);
    RUN_TEST(test_json_output_tool_calls_zero_count);

    // json_output_assistant_tool_calls (streaming) tests
    RUN_TEST(test_json_output_streaming_tool_calls_single);
    RUN_TEST(test_json_output_streaming_tool_calls_null_array);
    RUN_TEST(test_json_output_streaming_tool_calls_zero_count);

    return UNITY_END();
}
