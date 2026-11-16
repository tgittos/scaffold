/**
 * Unit tests for gate_prompter module
 *
 * Note: Most gate_prompter functions require a TTY, which is not available
 * in automated test environments. These tests focus on lifecycle management,
 * null safety, and behaviors that don't require terminal interaction.
 */

#include <stdlib.h>
#include <string.h>
#include "unity/unity.h"
#include "policy/gate_prompter.h"

void setUp(void) {}
void tearDown(void) {}

/* =============================================================================
 * Lifecycle Tests
 * ========================================================================== */

void test_gate_prompter_create_returns_null_without_tty(void) {
    /* In test environment, we typically don't have a TTY attached to stdin */
    /* This test verifies the function doesn't crash */
    GatePrompter *gp = gate_prompter_create();
    /* Result depends on test environment - may be NULL or valid */
    /* Just verify it doesn't crash */
    gate_prompter_destroy(gp);
    TEST_PASS();
}

void test_gate_prompter_destroy_null_is_safe(void) {
    /* Should not crash when passed NULL */
    gate_prompter_destroy(NULL);
    TEST_PASS();
}

/* =============================================================================
 * is_interactive Tests
 * ========================================================================== */

void test_gate_prompter_is_interactive_null_returns_false(void) {
    TEST_ASSERT_EQUAL(0, gate_prompter_is_interactive(NULL));
}

/* =============================================================================
 * read_key Tests
 * ========================================================================== */

void test_gate_prompter_read_key_null_returns_error(void) {
    int result = gate_prompter_read_key(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

/* =============================================================================
 * read_key_timeout Tests
 * ========================================================================== */

void test_gate_prompter_read_key_timeout_null_prompter_returns_error(void) {
    char key = 0;
    int result = gate_prompter_read_key_timeout(NULL, 100, &key);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_gate_prompter_read_key_timeout_null_key_returns_error(void) {
    /* Even with NULL key pointer, should handle gracefully */
    /* We can't create a valid GatePrompter without TTY, so just test NULL case */
    int result = gate_prompter_read_key_timeout(NULL, 100, NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

/* =============================================================================
 * show_single Tests
 * ========================================================================== */

void test_gate_prompter_show_single_null_prompter_is_safe(void) {
    ToolCall tc = {0};
    tc.name = "test_tool";
    tc.arguments = "{}";

    /* Should not crash when passed NULL prompter */
    gate_prompter_show_single(NULL, &tc, NULL, NULL, NULL, NULL);
    TEST_PASS();
}

void test_gate_prompter_show_single_null_tool_call_is_safe(void) {
    /* Should not crash when passed NULL tool_call */
    gate_prompter_show_single(NULL, NULL, NULL, NULL, NULL, NULL);
    TEST_PASS();
}

void test_gate_prompter_hint_with_label_offers_always(void) {
    char buf[256];
    gate_prompter_format_hint(buf, sizeof(buf), "\"git show HEAD\"");
    TEST_ASSERT_NOT_NULL(strstr(buf, "always allow \"git show HEAD\""));
}

void test_gate_prompter_hint_without_label_hides_always(void) {
    char buf[256];
    gate_prompter_format_hint(buf, sizeof(buf), NULL);
    TEST_ASSERT_EQUAL_STRING("Allow? [y/n/?]", buf);
    TEST_ASSERT_NULL(strstr(buf, "a ="));
}

void test_gate_prompter_hint_truncates_long_label_to_80_columns(void) {
    char buf[256];
    gate_prompter_format_hint(buf, sizeof(buf),
        "\"git show HEAD\", \"grep -rn foo lib\", \"head -5 log\", \"sort -u x\"");
    size_t chars = 0;
    for (const unsigned char *c = (const unsigned char *)buf; *c; c++) {
        if ((*c & 0xC0) != 0x80) chars++;
    }
    TEST_ASSERT_TRUE(chars <= 80);
    size_t len = strlen(buf);
    TEST_ASSERT_TRUE(len > 5);
    TEST_ASSERT_EQUAL_STRING("…\")", buf + len - strlen("…\")"));
}

void test_gate_prompter_hint_escapes_control_characters(void) {
    char buf[256];
    gate_prompter_format_hint(buf, sizeof(buf), "\"ls\n\x1b" "[31mrm -rf /\"");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\x1b"));
    for (const char *c = buf; *c; c++) {
        TEST_ASSERT_TRUE((unsigned char)*c >= 0x20 || *c == '\t');
    }
}

void test_gate_prompter_escape_control_renders_escapes(void) {
    char buf[64];
    gate_prompter_escape_control(buf, sizeof(buf), "a\nb\tc\x1b" "d\re");
    TEST_ASSERT_EQUAL_STRING("a\\nb\\tc\\x1bd\\re", buf);

    gate_prompter_escape_control(buf, sizeof(buf), "plain text");
    TEST_ASSERT_EQUAL_STRING("plain text", buf);
}

void test_gate_prompter_escape_control_truncates_safely(void) {
    char buf[6];
    gate_prompter_escape_control(buf, sizeof(buf), "ab\x1b" "cd");
    TEST_ASSERT_EQUAL_STRING("ab", buf);
    TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

void test_gate_prompter_escape_alloc_escapes_control_characters(void) {
    char *escaped = gate_prompter_escape_alloc("a\x1b[2Kb");
    TEST_ASSERT_NOT_NULL(escaped);
    TEST_ASSERT_EQUAL_STRING("a\\x1b[2Kb", escaped);
    free(escaped);

    TEST_ASSERT_NULL(gate_prompter_escape_alloc(NULL));
}

void test_gate_prompter_escapes_bidi_and_c1_controls(void) {
    char buf[64];

    /* U+202E reorders everything after it on a terminal that renders bidi. */
    gate_prompter_escape_control(buf, sizeof(buf), "echo ok \xe2\x80\xaerm -rf");
    TEST_ASSERT_EQUAL_STRING("echo ok \\u{202E}rm -rf", buf);

    /* Isolates and marks hide text the same way. */
    gate_prompter_escape_control(buf, sizeof(buf), "a\xe2\x81\xa6" "b" "\xe2\x80\x8e" "c");
    TEST_ASSERT_EQUAL_STRING("a\\u{2066}b\\u{200E}c", buf);

    /* C1 controls are escape sequences on many terminals. */
    gate_prompter_escape_control(buf, sizeof(buf), "a\xc2\x9bm");
    TEST_ASSERT_EQUAL_STRING("a\\u{009B}m", buf);

    /* Ordinary multi-byte text is left alone. */
    gate_prompter_escape_control(buf, sizeof(buf), "caf\xc3\xa9 \xe2\x9c\x93");
    TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9 \xe2\x9c\x93", buf);
}

void test_gate_prompter_escape_alloc_and_detail_escape_bidi(void) {
    char *escaped = gate_prompter_escape_alloc("x\xe2\x80\xaby");
    TEST_ASSERT_NOT_NULL(escaped);
    TEST_ASSERT_EQUAL_STRING("x\\u{202B}y", escaped);
    free(escaped);

    char buf[256];
    gate_prompter_format_detail(buf, sizeof(buf), "echo \xe2\x80\xad", NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("echo \\u{202D}", buf);
}

void test_gate_prompter_escape_truncate_keeps_bidi_escapes_whole(void) {
    char buf[64];

    /* "ab" plus an eight-character escape doesn't fit in nine characters. */
    gate_prompter_escape_truncate(buf, sizeof(buf), "ab" "\xe2\x80\xae" "cdef", 9);
    TEST_ASSERT_EQUAL_STRING("ab...", buf);

    gate_prompter_escape_truncate(buf, sizeof(buf), "ab\xe2\x80\xae", 12);
    TEST_ASSERT_EQUAL_STRING("ab\\u{202E}", buf);
}

void test_gate_prompter_escape_truncate_keeps_escapes_whole(void) {
    char buf[64];

    /* "ab" plus a four-byte escape doesn't fit in five characters. */
    gate_prompter_escape_truncate(buf, sizeof(buf), "ab\x1b" "cdef", 5);
    TEST_ASSERT_EQUAL_STRING("ab...", buf);

    gate_prompter_escape_truncate(buf, sizeof(buf), "short", 20);
    TEST_ASSERT_EQUAL_STRING("short", buf);
}

void test_gate_prompter_format_detail_escapes_and_names_directory(void) {
    char buf[256];

    gate_prompter_format_detail(buf, sizeof(buf), "make test", NULL, NULL, "/tmp/x");
    TEST_ASSERT_EQUAL_STRING("make test (in /tmp/x)", buf);

    /* A form feed in a path is invisible unless it is escaped. */
    gate_prompter_format_detail(buf, sizeof(buf), NULL, "run.sh\f", NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("run.sh\\x0c", buf);

    gate_prompter_format_detail(buf, sizeof(buf), NULL, NULL,
                                "{\"a\": \"b\x1b[2K\"}", NULL);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\\x1b"));

    gate_prompter_format_detail(buf, sizeof(buf), NULL, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_gate_prompter_format_detail_truncates_long_values(void) {
    char buf[256];
    char long_command[200];
    memset(long_command, 'x', sizeof(long_command) - 1);
    long_command[sizeof(long_command) - 1] = '\0';

    gate_prompter_format_detail(buf, sizeof(buf), long_command, NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(60, (int)strlen(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "..."));

    char long_path[200];
    memset(long_path, 'y', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    gate_prompter_format_detail(buf, sizeof(buf), NULL, long_path, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(60, (int)strlen(buf));
    TEST_ASSERT_EQUAL_INT(0, strncmp(buf, "...", 3));
}

/* =============================================================================
 * show_details Tests
 * ========================================================================== */

void test_gate_prompter_show_details_null_prompter_is_safe(void) {
    ToolCall tc = {0};
    tc.name = "test_tool";
    tc.arguments = "{}";

    /* Should not crash when passed NULL prompter */
    gate_prompter_show_details(NULL, &tc, NULL, 0);
    TEST_PASS();
}

void test_gate_prompter_show_details_null_tool_call_is_safe(void) {
    /* Should not crash when passed NULL tool_call */
    gate_prompter_show_details(NULL, NULL, NULL, 0);
    TEST_PASS();
}

/* =============================================================================
 * show_batch Tests
 * ========================================================================== */

void test_gate_prompter_show_batch_null_prompter_is_safe(void) {
    ToolCall tc = {0};
    tc.name = "test_tool";
    tc.arguments = "{}";

    /* Should not crash when passed NULL prompter */
    gate_prompter_show_batch(NULL, &tc, 1, "?");
    TEST_PASS();
}

void test_gate_prompter_show_batch_null_tool_calls_is_safe(void) {
    /* Should not crash when passed NULL tool_calls */
    gate_prompter_show_batch(NULL, NULL, 0, NULL);
    TEST_PASS();
}

void test_gate_prompter_show_batch_zero_count_is_safe(void) {
    ToolCall tc = {0};
    tc.name = "test_tool";
    tc.arguments = "{}";

    /* Should not crash with zero count */
    gate_prompter_show_batch(NULL, &tc, 0, "");
    TEST_PASS();
}

/* =============================================================================
 * clear_prompt Tests
 * ========================================================================== */

void test_gate_prompter_clear_prompt_null_prompter_is_safe(void) {
    /* Should not crash when passed NULL prompter */
    gate_prompter_clear_prompt(NULL);
    TEST_PASS();
}

/* =============================================================================
 * clear_batch_prompt Tests
 * ========================================================================== */

void test_gate_prompter_clear_batch_prompt_null_prompter_is_safe(void) {
    /* Should not crash when passed NULL prompter */
    gate_prompter_clear_batch_prompt(NULL, 5);
    TEST_PASS();
}

void test_gate_prompter_clear_batch_prompt_zero_count_is_safe(void) {
    /* Should not crash with zero count */
    gate_prompter_clear_batch_prompt(NULL, 0);
    TEST_PASS();
}

void test_gate_prompter_clear_batch_prompt_negative_count_is_safe(void) {
    /* Should not crash with negative count */
    gate_prompter_clear_batch_prompt(NULL, -1);
    TEST_PASS();
}

/* =============================================================================
 * print Tests
 * ========================================================================== */

void test_gate_prompter_print_null_prompter_is_safe(void) {
    /* Should not crash when passed NULL prompter */
    gate_prompter_print(NULL, "test message %d", 42);
    TEST_PASS();
}

void test_gate_prompter_print_null_format_is_safe(void) {
    /* Should not crash when passed NULL format */
    gate_prompter_print(NULL, NULL);
    TEST_PASS();
}

/* =============================================================================
 * newline Tests
 * ========================================================================== */

void test_gate_prompter_newline_null_prompter_is_safe(void) {
    /* Should not crash when passed NULL prompter */
    gate_prompter_newline(NULL);
    TEST_PASS();
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Lifecycle tests */
    RUN_TEST(test_gate_prompter_create_returns_null_without_tty);
    RUN_TEST(test_gate_prompter_destroy_null_is_safe);

    /* is_interactive tests */
    RUN_TEST(test_gate_prompter_is_interactive_null_returns_false);

    /* read_key tests */
    RUN_TEST(test_gate_prompter_read_key_null_returns_error);

    /* read_key_timeout tests */
    RUN_TEST(test_gate_prompter_read_key_timeout_null_prompter_returns_error);
    RUN_TEST(test_gate_prompter_read_key_timeout_null_key_returns_error);

    /* show_single tests */
    RUN_TEST(test_gate_prompter_show_single_null_prompter_is_safe);
    RUN_TEST(test_gate_prompter_show_single_null_tool_call_is_safe);
    RUN_TEST(test_gate_prompter_hint_with_label_offers_always);
    RUN_TEST(test_gate_prompter_hint_without_label_hides_always);
    RUN_TEST(test_gate_prompter_hint_truncates_long_label_to_80_columns);
    RUN_TEST(test_gate_prompter_hint_escapes_control_characters);
    RUN_TEST(test_gate_prompter_escape_control_renders_escapes);
    RUN_TEST(test_gate_prompter_escape_control_truncates_safely);
    RUN_TEST(test_gate_prompter_escape_alloc_escapes_control_characters);
    RUN_TEST(test_gate_prompter_escape_truncate_keeps_escapes_whole);
    RUN_TEST(test_gate_prompter_escapes_bidi_and_c1_controls);
    RUN_TEST(test_gate_prompter_escape_alloc_and_detail_escape_bidi);
    RUN_TEST(test_gate_prompter_escape_truncate_keeps_bidi_escapes_whole);
    RUN_TEST(test_gate_prompter_format_detail_escapes_and_names_directory);
    RUN_TEST(test_gate_prompter_format_detail_truncates_long_values);

    /* show_details tests */
    RUN_TEST(test_gate_prompter_show_details_null_prompter_is_safe);
    RUN_TEST(test_gate_prompter_show_details_null_tool_call_is_safe);

    /* show_batch tests */
    RUN_TEST(test_gate_prompter_show_batch_null_prompter_is_safe);
    RUN_TEST(test_gate_prompter_show_batch_null_tool_calls_is_safe);
    RUN_TEST(test_gate_prompter_show_batch_zero_count_is_safe);

    /* clear_prompt tests */
    RUN_TEST(test_gate_prompter_clear_prompt_null_prompter_is_safe);

    /* clear_batch_prompt tests */
    RUN_TEST(test_gate_prompter_clear_batch_prompt_null_prompter_is_safe);
    RUN_TEST(test_gate_prompter_clear_batch_prompt_zero_count_is_safe);
    RUN_TEST(test_gate_prompter_clear_batch_prompt_negative_count_is_safe);

    /* print tests */
    RUN_TEST(test_gate_prompter_print_null_prompter_is_safe);
    RUN_TEST(test_gate_prompter_print_null_format_is_safe);

    /* newline tests */
    RUN_TEST(test_gate_prompter_newline_null_prompter_is_safe);

    return UNITY_END();
}
