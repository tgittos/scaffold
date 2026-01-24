/**
 * Unit tests for path normalization module
 */

#include "../test/unity/unity.h"
#include "../src/core/path_normalize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
    /* No setup needed */
}

void tearDown(void) {
    /* No teardown needed */
}

/* =============================================================================
 * Basic Normalization Tests
 * ========================================================================== */

void test_normalize_path_simple_relative(void) {
    NormalizedPath *np = normalize_path("foo/bar.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("foo/bar.c", np->normalized);
    TEST_ASSERT_EQUAL_STRING("bar.c", np->basename);
    TEST_ASSERT_EQUAL(0, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_simple_absolute(void) {
    NormalizedPath *np = normalize_path("/home/user/file.txt");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/home/user/file.txt", np->normalized);
    TEST_ASSERT_EQUAL_STRING("file.txt", np->basename);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_single_file(void) {
    NormalizedPath *np = normalize_path("file.txt");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("file.txt", np->normalized);
    TEST_ASSERT_EQUAL_STRING("file.txt", np->basename);
    TEST_ASSERT_EQUAL(0, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_root(void) {
    NormalizedPath *np = normalize_path("/");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/", np->normalized);
    /* Basename of "/" is empty string after the slash */
    TEST_ASSERT_EQUAL_STRING("", np->basename);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

/* =============================================================================
 * Trailing Slash Tests
 * ========================================================================== */

void test_normalize_path_removes_trailing_slash(void) {
    NormalizedPath *np = normalize_path("foo/bar/");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("foo/bar", np->normalized);
    TEST_ASSERT_EQUAL_STRING("bar", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_removes_multiple_trailing_slashes(void) {
    NormalizedPath *np = normalize_path("foo/bar///");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("foo/bar", np->normalized);
    TEST_ASSERT_EQUAL_STRING("bar", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_preserves_root_slash(void) {
    /* Root "/" should not become empty string */
    NormalizedPath *np = normalize_path("///");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/", np->normalized);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

/* =============================================================================
 * Duplicate Slash Tests
 * ========================================================================== */

void test_normalize_path_collapses_duplicate_slashes(void) {
    NormalizedPath *np = normalize_path("foo//bar//baz.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("foo/bar/baz.c", np->normalized);
    TEST_ASSERT_EQUAL_STRING("baz.c", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_collapses_many_slashes(void) {
    NormalizedPath *np = normalize_path("////foo////bar////");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/foo/bar", np->normalized);
    TEST_ASSERT_EQUAL_STRING("bar", np->basename);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

/* =============================================================================
 * NULL and Empty Input Tests
 * ========================================================================== */

void test_normalize_path_null_returns_null(void) {
    NormalizedPath *np = normalize_path(NULL);
    TEST_ASSERT_NULL(np);
}

void test_normalize_path_empty_returns_null(void) {
    NormalizedPath *np = normalize_path("");
    TEST_ASSERT_NULL(np);
}

void test_free_normalized_path_null_safe(void) {
    /* Should not crash */
    free_normalized_path(NULL);
}

/* =============================================================================
 * Basename Extraction Tests
 * ========================================================================== */

void test_normalize_path_basename_env_file(void) {
    NormalizedPath *np = normalize_path("/home/user/.env");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING(".env", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_basename_env_local(void) {
    NormalizedPath *np = normalize_path("/project/.env.local");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING(".env.local", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_basename_ralph_config(void) {
    NormalizedPath *np = normalize_path("/project/ralph.config.json");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("ralph.config.json", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_basename_dot_ralph_config(void) {
    NormalizedPath *np = normalize_path("/home/.ralph/config.json");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("config.json", np->basename);
    free_normalized_path(np);
}

/* =============================================================================
 * Platform-Specific Tests
 * ========================================================================== */

#ifdef _WIN32
/* Windows-specific tests */

void test_normalize_path_windows_backslash_conversion(void) {
    NormalizedPath *np = normalize_path("foo\\bar\\baz.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("foo/bar/baz.c", np->normalized);
    TEST_ASSERT_EQUAL_STRING("baz.c", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_windows_mixed_slashes(void) {
    NormalizedPath *np = normalize_path("foo\\bar/baz\\qux.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("foo/bar/baz/qux.c", np->normalized);
    free_normalized_path(np);
}

void test_normalize_path_windows_lowercase(void) {
    NormalizedPath *np = normalize_path("C:\\Users\\ADMIN\\Documents\\File.TXT");
    TEST_ASSERT_NOT_NULL(np);
    /* Should be lowercased */
    TEST_ASSERT_EQUAL_STRING("/c/users/admin/documents/file.txt", np->normalized);
    free_normalized_path(np);
}

void test_normalize_path_windows_drive_letter(void) {
    NormalizedPath *np = normalize_path("C:\\foo\\bar.c");
    TEST_ASSERT_NOT_NULL(np);
    /* C:\ -> /c/ */
    TEST_ASSERT_EQUAL_STRING("/c/foo/bar.c", np->normalized);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_windows_drive_letter_no_slash(void) {
    /* C:foo (relative to current dir on C:) -> /c/foo */
    NormalizedPath *np = normalize_path("C:foo\\bar.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/c/foo/bar.c", np->normalized);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_windows_drive_letter_lowercase(void) {
    NormalizedPath *np = normalize_path("d:\\Projects\\test.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/d/projects/test.c", np->normalized);
    free_normalized_path(np);
}

void test_normalize_path_windows_unc_path(void) {
    /* \\server\share -> /unc/server/share */
    NormalizedPath *np = normalize_path("\\\\server\\share\\file.txt");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/unc/server/share/file.txt", np->normalized);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_windows_unc_path_forward_slash(void) {
    /* //server/share -> /unc/server/share */
    NormalizedPath *np = normalize_path("//server/share/file.txt");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/unc/server/share/file.txt", np->normalized);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

#else
/* POSIX-specific tests */

void test_normalize_path_posix_case_preserved(void) {
    /* POSIX is case-sensitive, so case should be preserved */
    NormalizedPath *np = normalize_path("/Home/USER/Documents/File.TXT");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/Home/USER/Documents/File.TXT", np->normalized);
    free_normalized_path(np);
}

void test_normalize_path_posix_env_case_sensitive(void) {
    /* .ENV is not the same as .env on POSIX */
    NormalizedPath *np = normalize_path("/project/.ENV");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING(".ENV", np->basename);
    free_normalized_path(np);
}

#endif

/* =============================================================================
 * Basename Comparison Tests
 * ========================================================================== */

void test_path_basename_cmp_equal(void) {
    TEST_ASSERT_EQUAL(0, path_basename_cmp("file.txt", "file.txt"));
    TEST_ASSERT_EQUAL(0, path_basename_cmp(".env", ".env"));
    TEST_ASSERT_EQUAL(0, path_basename_cmp("ralph.config.json", "ralph.config.json"));
}

void test_path_basename_cmp_not_equal(void) {
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp("file.txt", "other.txt"));
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp(".env", ".env.local"));
}

void test_path_basename_cmp_null(void) {
    TEST_ASSERT_EQUAL(0, path_basename_cmp(NULL, NULL));
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp("file.txt", NULL));
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp(NULL, "file.txt"));
}

#ifdef _WIN32
void test_path_basename_cmp_windows_case_insensitive(void) {
    TEST_ASSERT_EQUAL(0, path_basename_cmp("FILE.TXT", "file.txt"));
    TEST_ASSERT_EQUAL(0, path_basename_cmp(".ENV", ".env"));
    TEST_ASSERT_EQUAL(0, path_basename_cmp("Ralph.Config.JSON", "ralph.config.json"));
}
#else
void test_path_basename_cmp_posix_case_sensitive(void) {
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp("FILE.TXT", "file.txt"));
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp(".ENV", ".env"));
    TEST_ASSERT_NOT_EQUAL(0, path_basename_cmp("Ralph.Config.JSON", "ralph.config.json"));
}
#endif

/* =============================================================================
 * Basename Prefix Tests
 * ========================================================================== */

void test_path_basename_has_prefix_match(void) {
    TEST_ASSERT_EQUAL(1, path_basename_has_prefix(".env.local", ".env."));
    TEST_ASSERT_EQUAL(1, path_basename_has_prefix(".env.production", ".env."));
    TEST_ASSERT_EQUAL(1, path_basename_has_prefix("test_foo.c", "test_"));
}

void test_path_basename_has_prefix_no_match(void) {
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix(".env", ".env."));
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix("foo.c", "test_"));
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix("production.env", ".env"));
}

void test_path_basename_has_prefix_empty(void) {
    /* Empty prefix matches everything */
    TEST_ASSERT_EQUAL(1, path_basename_has_prefix("anything", ""));
}

void test_path_basename_has_prefix_null(void) {
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix(NULL, ".env."));
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix(".env.local", NULL));
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix(NULL, NULL));
}

#ifdef _WIN32
void test_path_basename_has_prefix_windows_case_insensitive(void) {
    TEST_ASSERT_EQUAL(1, path_basename_has_prefix(".ENV.LOCAL", ".env."));
    TEST_ASSERT_EQUAL(1, path_basename_has_prefix(".Env.Production", ".ENV."));
}
#else
void test_path_basename_has_prefix_posix_case_sensitive(void) {
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix(".ENV.LOCAL", ".env."));
    TEST_ASSERT_EQUAL(0, path_basename_has_prefix(".Env.Production", ".ENV."));
}
#endif

/* =============================================================================
 * Edge Cases
 * ========================================================================== */

void test_normalize_path_dot_current_dir(void) {
    NormalizedPath *np = normalize_path(".");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING(".", np->normalized);
    TEST_ASSERT_EQUAL_STRING(".", np->basename);
    TEST_ASSERT_EQUAL(0, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_dot_dot_parent_dir(void) {
    NormalizedPath *np = normalize_path("..");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("..", np->normalized);
    TEST_ASSERT_EQUAL_STRING("..", np->basename);
    TEST_ASSERT_EQUAL(0, np->is_absolute);
    free_normalized_path(np);
}

void test_normalize_path_relative_with_dots(void) {
    /* Note: This does NOT resolve .. - just normalizes slashes */
    NormalizedPath *np = normalize_path("./foo/../bar/file.c");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("./foo/../bar/file.c", np->normalized);
    TEST_ASSERT_EQUAL_STRING("file.c", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_hidden_file(void) {
    NormalizedPath *np = normalize_path("/home/user/.bashrc");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING(".bashrc", np->basename);
    free_normalized_path(np);
}

void test_normalize_path_deep_nested(void) {
    NormalizedPath *np = normalize_path("/a/b/c/d/e/f/g/h/i/j/file.txt");
    TEST_ASSERT_NOT_NULL(np);
    TEST_ASSERT_EQUAL_STRING("/a/b/c/d/e/f/g/h/i/j/file.txt", np->normalized);
    TEST_ASSERT_EQUAL_STRING("file.txt", np->basename);
    TEST_ASSERT_EQUAL(1, np->is_absolute);
    free_normalized_path(np);
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Basic normalization tests */
    RUN_TEST(test_normalize_path_simple_relative);
    RUN_TEST(test_normalize_path_simple_absolute);
    RUN_TEST(test_normalize_path_single_file);
    RUN_TEST(test_normalize_path_root);

    /* Trailing slash tests */
    RUN_TEST(test_normalize_path_removes_trailing_slash);
    RUN_TEST(test_normalize_path_removes_multiple_trailing_slashes);
    RUN_TEST(test_normalize_path_preserves_root_slash);

    /* Duplicate slash tests */
    RUN_TEST(test_normalize_path_collapses_duplicate_slashes);
    RUN_TEST(test_normalize_path_collapses_many_slashes);

    /* NULL and empty input tests */
    RUN_TEST(test_normalize_path_null_returns_null);
    RUN_TEST(test_normalize_path_empty_returns_null);
    RUN_TEST(test_free_normalized_path_null_safe);

    /* Basename extraction tests */
    RUN_TEST(test_normalize_path_basename_env_file);
    RUN_TEST(test_normalize_path_basename_env_local);
    RUN_TEST(test_normalize_path_basename_ralph_config);
    RUN_TEST(test_normalize_path_basename_dot_ralph_config);

    /* Platform-specific tests */
#ifdef _WIN32
    RUN_TEST(test_normalize_path_windows_backslash_conversion);
    RUN_TEST(test_normalize_path_windows_mixed_slashes);
    RUN_TEST(test_normalize_path_windows_lowercase);
    RUN_TEST(test_normalize_path_windows_drive_letter);
    RUN_TEST(test_normalize_path_windows_drive_letter_no_slash);
    RUN_TEST(test_normalize_path_windows_drive_letter_lowercase);
    RUN_TEST(test_normalize_path_windows_unc_path);
    RUN_TEST(test_normalize_path_windows_unc_path_forward_slash);
#else
    RUN_TEST(test_normalize_path_posix_case_preserved);
    RUN_TEST(test_normalize_path_posix_env_case_sensitive);
#endif

    /* Basename comparison tests */
    RUN_TEST(test_path_basename_cmp_equal);
    RUN_TEST(test_path_basename_cmp_not_equal);
    RUN_TEST(test_path_basename_cmp_null);
#ifdef _WIN32
    RUN_TEST(test_path_basename_cmp_windows_case_insensitive);
#else
    RUN_TEST(test_path_basename_cmp_posix_case_sensitive);
#endif

    /* Basename prefix tests */
    RUN_TEST(test_path_basename_has_prefix_match);
    RUN_TEST(test_path_basename_has_prefix_no_match);
    RUN_TEST(test_path_basename_has_prefix_empty);
    RUN_TEST(test_path_basename_has_prefix_null);
#ifdef _WIN32
    RUN_TEST(test_path_basename_has_prefix_windows_case_insensitive);
#else
    RUN_TEST(test_path_basename_has_prefix_posix_case_sensitive);
#endif

    /* Edge cases */
    RUN_TEST(test_normalize_path_dot_current_dir);
    RUN_TEST(test_normalize_path_dot_dot_parent_dir);
    RUN_TEST(test_normalize_path_relative_with_dots);
    RUN_TEST(test_normalize_path_hidden_file);
    RUN_TEST(test_normalize_path_deep_nested);

    return UNITY_END();
}
