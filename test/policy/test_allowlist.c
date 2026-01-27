/**
 * Unit tests for allowlist module
 */

#include "unity/unity.h"
#include "../src/policy/allowlist.h"

void setUp(void) {}
void tearDown(void) {}

/* =============================================================================
 * Lifecycle Tests
 * ========================================================================== */

void test_allowlist_create_returns_valid_list(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);
    TEST_ASSERT_EQUAL(0, allowlist_regex_count(al));
    TEST_ASSERT_EQUAL(0, allowlist_shell_count(al));
    allowlist_destroy(al);
}

void test_allowlist_destroy_null_is_safe(void) {
    allowlist_destroy(NULL);
    TEST_PASS();
}

/* =============================================================================
 * Regex Entry Tests
 * ========================================================================== */

void test_allowlist_add_regex_increments_count(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    int ret = allowlist_add_regex(al, "file", "^/tmp/.*$");
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, allowlist_regex_count(al));

    allowlist_destroy(al);
}

void test_allowlist_add_regex_invalid_pattern(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    /* Invalid regex - unmatched bracket */
    int ret = allowlist_add_regex(al, "file", "[invalid");
    TEST_ASSERT_EQUAL(-1, ret);
    TEST_ASSERT_EQUAL(0, allowlist_regex_count(al));

    allowlist_destroy(al);
}

void test_allowlist_add_regex_null_params(void) {
    Allowlist *al = allowlist_create();

    TEST_ASSERT_EQUAL(-1, allowlist_add_regex(NULL, "file", ".*"));
    TEST_ASSERT_EQUAL(-1, allowlist_add_regex(al, NULL, ".*"));
    TEST_ASSERT_EQUAL(-1, allowlist_add_regex(al, "file", NULL));

    allowlist_destroy(al);
}

void test_allowlist_check_regex_matches(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    allowlist_add_regex(al, "file", "^/tmp/.*\\.txt$");

    TEST_ASSERT_EQUAL(ALLOWLIST_MATCHED,
                      allowlist_check_regex(al, "file", "/tmp/test.txt"));
    TEST_ASSERT_EQUAL(ALLOWLIST_MATCHED,
                      allowlist_check_regex(al, "file", "/tmp/foo/bar.txt"));

    allowlist_destroy(al);
}

void test_allowlist_check_regex_no_match(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    allowlist_add_regex(al, "file", "^/tmp/.*\\.txt$");

    /* Wrong tool */
    TEST_ASSERT_EQUAL(ALLOWLIST_NO_MATCH,
                      allowlist_check_regex(al, "other", "/tmp/test.txt"));
    /* Wrong path */
    TEST_ASSERT_EQUAL(ALLOWLIST_NO_MATCH,
                      allowlist_check_regex(al, "file", "/home/test.txt"));
    /* Wrong extension */
    TEST_ASSERT_EQUAL(ALLOWLIST_NO_MATCH,
                      allowlist_check_regex(al, "file", "/tmp/test.doc"));

    allowlist_destroy(al);
}

/* =============================================================================
 * Shell Entry Tests
 * ========================================================================== */

void test_allowlist_add_shell_increments_count(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    const char *prefix[] = {"git", "status"};
    int ret = allowlist_add_shell(al, prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, allowlist_shell_count(al));

    allowlist_destroy(al);
}

void test_allowlist_add_shell_null_params(void) {
    Allowlist *al = allowlist_create();

    const char *prefix[] = {"git"};
    TEST_ASSERT_EQUAL(-1, allowlist_add_shell(NULL, prefix, 1, SHELL_TYPE_UNKNOWN));
    TEST_ASSERT_EQUAL(-1, allowlist_add_shell(al, NULL, 1, SHELL_TYPE_UNKNOWN));
    TEST_ASSERT_EQUAL(-1, allowlist_add_shell(al, prefix, 0, SHELL_TYPE_UNKNOWN));

    allowlist_destroy(al);
}

void test_allowlist_check_shell_matches(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    const char *prefix[] = {"git", "status"};
    allowlist_add_shell(al, prefix, 2, SHELL_TYPE_UNKNOWN);

    const char *cmd1[] = {"git", "status"};
    TEST_ASSERT_EQUAL(ALLOWLIST_MATCHED,
                      allowlist_check_shell(al, cmd1, 2, SHELL_TYPE_UNKNOWN));

    /* Extra args should still match */
    const char *cmd2[] = {"git", "status", "--short"};
    TEST_ASSERT_EQUAL(ALLOWLIST_MATCHED,
                      allowlist_check_shell(al, cmd2, 3, SHELL_TYPE_UNKNOWN));

    allowlist_destroy(al);
}

void test_allowlist_check_shell_no_match(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    const char *prefix[] = {"git", "status"};
    allowlist_add_shell(al, prefix, 2, SHELL_TYPE_UNKNOWN);

    /* Different command */
    const char *cmd1[] = {"git", "commit"};
    TEST_ASSERT_EQUAL(ALLOWLIST_NO_MATCH,
                      allowlist_check_shell(al, cmd1, 2, SHELL_TYPE_UNKNOWN));

    /* Too few args */
    const char *cmd2[] = {"git"};
    TEST_ASSERT_EQUAL(ALLOWLIST_NO_MATCH,
                      allowlist_check_shell(al, cmd2, 1, SHELL_TYPE_UNKNOWN));

    allowlist_destroy(al);
}

/* =============================================================================
 * Clear Session Tests
 * ========================================================================== */

void test_allowlist_clear_session_removes_entries(void) {
    Allowlist *al = allowlist_create();
    TEST_ASSERT_NOT_NULL(al);

    /* Add some entries */
    allowlist_add_regex(al, "file", "^/static/.*$");
    allowlist_add_regex(al, "file", "^/session/.*$");

    const char *prefix1[] = {"make"};
    const char *prefix2[] = {"npm", "run"};
    allowlist_add_shell(al, prefix1, 1, SHELL_TYPE_UNKNOWN);
    allowlist_add_shell(al, prefix2, 2, SHELL_TYPE_UNKNOWN);

    TEST_ASSERT_EQUAL(2, allowlist_regex_count(al));
    TEST_ASSERT_EQUAL(2, allowlist_shell_count(al));

    /* Clear session entries, keeping 1 of each */
    allowlist_clear_session(al, 1, 1);

    TEST_ASSERT_EQUAL(1, allowlist_regex_count(al));
    TEST_ASSERT_EQUAL(1, allowlist_shell_count(al));

    allowlist_destroy(al);
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Lifecycle tests */
    RUN_TEST(test_allowlist_create_returns_valid_list);
    RUN_TEST(test_allowlist_destroy_null_is_safe);

    /* Regex entry tests */
    RUN_TEST(test_allowlist_add_regex_increments_count);
    RUN_TEST(test_allowlist_add_regex_invalid_pattern);
    RUN_TEST(test_allowlist_add_regex_null_params);
    RUN_TEST(test_allowlist_check_regex_matches);
    RUN_TEST(test_allowlist_check_regex_no_match);

    /* Shell entry tests */
    RUN_TEST(test_allowlist_add_shell_increments_count);
    RUN_TEST(test_allowlist_add_shell_null_params);
    RUN_TEST(test_allowlist_check_shell_matches);
    RUN_TEST(test_allowlist_check_shell_no_match);

    /* Clear session tests */
    RUN_TEST(test_allowlist_clear_session_removes_entries);

    return UNITY_END();
}
