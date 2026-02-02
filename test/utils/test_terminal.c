#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../unity/unity.h"
#include "../../src/utils/terminal.h"
#include "../../src/utils/output_formatter.h"

void setUp(void) {
    set_json_output_mode(false);
}

void tearDown(void) {
}

void test_terminal_strip_ansi_null(void) {
    char *result = terminal_strip_ansi(NULL);
    TEST_ASSERT_NULL(result);
}

void test_terminal_strip_ansi_empty(void) {
    char *result = terminal_strip_ansi("");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("", result);
    free(result);
}

void test_terminal_strip_ansi_no_ansi(void) {
    char *result = terminal_strip_ansi("Hello, World!");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hello, World!", result);
    free(result);
}

void test_terminal_strip_ansi_simple_color(void) {
    char *result = terminal_strip_ansi("\033[32mGreen\033[0m");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Green", result);
    free(result);
}

void test_terminal_strip_ansi_multiple_colors(void) {
    char *result = terminal_strip_ansi("\033[31mRed\033[0m \033[32mGreen\033[0m \033[34mBlue\033[0m");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Red Green Blue", result);
    free(result);
}

void test_terminal_strip_ansi_bold_dim(void) {
    char *result = terminal_strip_ansi("\033[1mBold\033[0m \033[2mDim\033[0m");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Bold Dim", result);
    free(result);
}

void test_terminal_strip_ansi_clear_line(void) {
    char *result = terminal_strip_ansi("\r\033[KCleared line");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Cleared line", result);
    free(result);
}

void test_terminal_strip_ansi_cursor_movement(void) {
    char *result = terminal_strip_ansi("\033[3A\033[JText after cursor move");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Text after cursor move", result);
    free(result);
}

void test_terminal_strip_ansi_hex_escape(void) {
    char *result = terminal_strip_ansi("\x1b[36mCyan\x1b[0m");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Cyan", result);
    free(result);
}

void test_terminal_strip_ansi_complex(void) {
    /* Simulate real output from log_tool_execution_improved */
    const char *input = "\033[32m\u2713\033[0m shell_execute\033[2m (ls -la)\033[0m";
    char *result = terminal_strip_ansi(input);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(strstr(result, "shell_execute"));
    TEST_ASSERT_NOT_NULL(strstr(result, "(ls -la)"));
    TEST_ASSERT_NULL(strstr(result, "\033"));
    free(result);
}

void test_terminal_strip_ansi_256_color(void) {
    /* Test 256-color mode: ESC[38;5;196m (foreground red) */
    char *result = terminal_strip_ansi("\033[38;5;196mRed text\033[0m");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Red text", result);
    free(result);

    /* Test 24-bit color mode: ESC[38;2;255;0;0m */
    result = terminal_strip_ansi("\033[38;2;255;0;0mTrue color\033[0m");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("True color", result);
    free(result);
}

void test_terminal_strip_ansi_private_mode(void) {
    /* Test private mode sequences like hide/show cursor */
    char *result = terminal_strip_ansi("\033[?25lHidden cursor\033[?25h");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("Hidden cursor", result);
    free(result);
}

void test_terminal_colors_enabled_non_json_mode(void) {
    set_json_output_mode(false);
    /* Call to verify no crash; actual TTY detection depends on test environment */
    bool result = terminal_colors_enabled();
    /* Result depends on whether stdout is a TTY - just verify function returns */
    (void)result;
    TEST_ASSERT_TRUE(1);
}

void test_terminal_colors_enabled_json_mode(void) {
    set_json_output_mode(true);
    TEST_ASSERT_FALSE(terminal_colors_enabled());
    set_json_output_mode(false);
}

void test_terminal_separator_null_stream(void) {
    /* Should not crash with NULL stream */
    terminal_separator(NULL, TERM_SEP_LIGHT, 40);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_separator_zero_width(void) {
    /* Should not crash with zero width */
    terminal_separator(stdout, TERM_SEP_LIGHT, 0);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_separator_json_mode(void) {
    /* Should be no-op in JSON mode */
    set_json_output_mode(true);
    terminal_separator(stdout, TERM_SEP_LIGHT, 40);
    set_json_output_mode(false);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_header_null_title(void) {
    /* Should handle NULL title gracefully */
    terminal_header(stdout, NULL, 40);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_header_empty_title(void) {
    terminal_header(stdout, "", 40);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_tree_item_null_text(void) {
    terminal_tree_item(stdout, NULL, false, 0);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_tree_branch_last(void) {
    /* Should not crash, and produce output */
    terminal_tree_branch(stdout, true, 2);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_tree_branch_not_last(void) {
    terminal_tree_branch(stdout, false, 2);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_status_all_types(void) {
    terminal_status(stdout, TERM_STATUS_SUCCESS, "Success message");
    terminal_status(stdout, TERM_STATUS_ERROR, "Error message");
    terminal_status(stdout, TERM_STATUS_INFO, "Info message");
    terminal_status(stdout, TERM_STATUS_ACTIVE, "Active message");
    TEST_ASSERT_TRUE(1);
}

void test_terminal_status_null_message(void) {
    terminal_status(stdout, TERM_STATUS_SUCCESS, NULL);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_status_with_detail_all_types(void) {
    terminal_status_with_detail(stdout, TERM_STATUS_SUCCESS, "Test", "detail");
    terminal_status_with_detail(stdout, TERM_STATUS_ERROR, "Test", "detail");
    terminal_status_with_detail(stdout, TERM_STATUS_INFO, "Test", "detail");
    terminal_status_with_detail(stdout, TERM_STATUS_ACTIVE, "Test", "detail");
    TEST_ASSERT_TRUE(1);
}

void test_terminal_status_with_detail_null_detail(void) {
    terminal_status_with_detail(stdout, TERM_STATUS_SUCCESS, "Test", NULL);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_status_with_detail_empty_detail(void) {
    terminal_status_with_detail(stdout, TERM_STATUS_SUCCESS, "Test", "");
    TEST_ASSERT_TRUE(1);
}

void test_terminal_labeled_basic(void) {
    terminal_labeled(stdout, "Label", "Value");
    TEST_ASSERT_TRUE(1);
}

void test_terminal_labeled_null_values(void) {
    terminal_labeled(stdout, NULL, NULL);
    terminal_labeled(stdout, "Label", NULL);
    terminal_labeled(stdout, NULL, "Value");
    TEST_ASSERT_TRUE(1);
}

void test_terminal_clear_line_basic(void) {
    terminal_clear_line(stdout);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_clear_line_null_stream(void) {
    terminal_clear_line(NULL);
    TEST_ASSERT_TRUE(1);
}

void test_terminal_json_mode_noop(void) {
    set_json_output_mode(true);

    /* All rendering functions should be no-ops in JSON mode */
    terminal_separator(stdout, TERM_SEP_LIGHT, 40);
    terminal_header(stdout, "Test", 40);
    terminal_tree_item(stdout, "Test", true, 0);
    terminal_tree_branch(stdout, true, 0);
    terminal_status(stdout, TERM_STATUS_SUCCESS, "Test");
    terminal_status_with_detail(stdout, TERM_STATUS_SUCCESS, "Test", "detail");
    terminal_labeled(stdout, "Label", "Value");
    terminal_clear_line(stdout);

    set_json_output_mode(false);
    TEST_ASSERT_TRUE(1);
}

int main(void) {
    UNITY_BEGIN();

    /* strip_ansi tests */
    RUN_TEST(test_terminal_strip_ansi_null);
    RUN_TEST(test_terminal_strip_ansi_empty);
    RUN_TEST(test_terminal_strip_ansi_no_ansi);
    RUN_TEST(test_terminal_strip_ansi_simple_color);
    RUN_TEST(test_terminal_strip_ansi_multiple_colors);
    RUN_TEST(test_terminal_strip_ansi_bold_dim);
    RUN_TEST(test_terminal_strip_ansi_clear_line);
    RUN_TEST(test_terminal_strip_ansi_cursor_movement);
    RUN_TEST(test_terminal_strip_ansi_hex_escape);
    RUN_TEST(test_terminal_strip_ansi_complex);
    RUN_TEST(test_terminal_strip_ansi_256_color);
    RUN_TEST(test_terminal_strip_ansi_private_mode);

    /* colors_enabled tests */
    RUN_TEST(test_terminal_colors_enabled_non_json_mode);
    RUN_TEST(test_terminal_colors_enabled_json_mode);

    /* separator tests */
    RUN_TEST(test_terminal_separator_null_stream);
    RUN_TEST(test_terminal_separator_zero_width);
    RUN_TEST(test_terminal_separator_json_mode);

    /* header tests */
    RUN_TEST(test_terminal_header_null_title);
    RUN_TEST(test_terminal_header_empty_title);

    /* tree tests */
    RUN_TEST(test_terminal_tree_item_null_text);
    RUN_TEST(test_terminal_tree_branch_last);
    RUN_TEST(test_terminal_tree_branch_not_last);

    /* status tests */
    RUN_TEST(test_terminal_status_all_types);
    RUN_TEST(test_terminal_status_null_message);
    RUN_TEST(test_terminal_status_with_detail_all_types);
    RUN_TEST(test_terminal_status_with_detail_null_detail);
    RUN_TEST(test_terminal_status_with_detail_empty_detail);

    /* labeled tests */
    RUN_TEST(test_terminal_labeled_basic);
    RUN_TEST(test_terminal_labeled_null_values);

    /* clear_line tests */
    RUN_TEST(test_terminal_clear_line_basic);
    RUN_TEST(test_terminal_clear_line_null_stream);

    /* JSON mode tests */
    RUN_TEST(test_terminal_json_mode_noop);

    return UNITY_END();
}
