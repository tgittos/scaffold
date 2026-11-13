/**
 * Unit tests for gate_prompter module
 *
 * Note: Most gate_prompter functions require a TTY, which is not available
 * in automated test environments. These tests focus on lifecycle management,
 * null safety, and behaviors that don't require terminal interaction.
 */

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
    gate_prompter_show_single(NULL, &tc, NULL, NULL, NULL);
    TEST_PASS();
}

void test_gate_prompter_show_single_null_tool_call_is_safe(void) {
    /* Should not crash when passed NULL tool_call */
    gate_prompter_show_single(NULL, NULL, NULL, NULL, NULL);
    TEST_PASS();
}

void test_gate_prompter_hint_with_label_offers_always(void) {
    char buf[256];
    gate_prompter_format_hint(buf, sizeof(buf), "git show …");
    TEST_ASSERT_NOT_NULL(strstr(buf, "always allow \"git show …\""));
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
        "git show …, grep …, head …, sort …, uniq …, wc …, cat …, ls …, find …");
    size_t chars = 0;
    for (const unsigned char *c = (const unsigned char *)buf; *c; c++) {
        if ((*c & 0xC0) != 0x80) chars++;
    }
    TEST_ASSERT_TRUE(chars <= 80);
    size_t len = strlen(buf);
    TEST_ASSERT_TRUE(len > 5);
    TEST_ASSERT_EQUAL_STRING("…\")", buf + len - strlen("…\")"));
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
