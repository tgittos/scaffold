/**
 * Unit tests for protected files detection module
 */

#include "../test/unity/unity.h"
#include "policy/protected_files.h"
#include "policy/path_normalize.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test directory for creating temporary protected files */
static char test_dir[256];
static int test_dir_created = 0;

void setUp(void) {
    /* Create a unique test directory for each test run */
    if (!test_dir_created) {
        snprintf(test_dir, sizeof(test_dir), "/tmp/test_protected_%d", (int)getpid());
        mkdir(test_dir, 0755);
        test_dir_created = 1;
    }

    /* Reset protected files module state between tests */
    protected_files_cleanup();
}

void tearDown(void) {
    /* Cleanup is handled after all tests */
}

/* Helper to create a test file */
static int create_test_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "test content\n");
    fclose(f);
    return 0;
}

/* Helper to remove a test file */
static void remove_test_file(const char *path) {
    unlink(path);
}

/* =============================================================================
 * Pattern Access Tests
 * ========================================================================== */

void test_get_protected_basename_patterns_returns_array(void) {
    const char **patterns = get_protected_basename_patterns();
    TEST_ASSERT_NOT_NULL(patterns);

    /* Should contain known protected basenames */
    int found_ralph_config = 0;
    int found_env = 0;

    for (int i = 0; patterns[i]; i++) {
        if (strcmp(patterns[i], "ralph.config.json") == 0) found_ralph_config = 1;
        if (strcmp(patterns[i], ".env") == 0) found_env = 1;
    }

    TEST_ASSERT_EQUAL(1, found_ralph_config);
    TEST_ASSERT_EQUAL(1, found_env);
}

void test_get_protected_prefix_patterns_returns_array(void) {
    const char **patterns = get_protected_prefix_patterns();
    TEST_ASSERT_NOT_NULL(patterns);

    /* Should contain .env. prefix */
    int found_env_prefix = 0;

    for (int i = 0; patterns[i]; i++) {
        if (strcmp(patterns[i], ".env.") == 0) found_env_prefix = 1;
    }

    TEST_ASSERT_EQUAL(1, found_env_prefix);
}

void test_get_protected_glob_patterns_returns_array(void) {
    const char **patterns = get_protected_glob_patterns();
    TEST_ASSERT_NOT_NULL(patterns);

    /* Should contain expected glob patterns */
    int found_ralph_glob = 0;
    int found_env_glob = 0;
    int found_ralph_dir_glob = 0;

    for (int i = 0; patterns[i]; i++) {
        if (strcmp(patterns[i], "**/ralph.config.json") == 0) found_ralph_glob = 1;
        if (strcmp(patterns[i], "**/.env") == 0) found_env_glob = 1;
        if (strcmp(patterns[i], "**/.ralph/config.json") == 0) found_ralph_dir_glob = 1;
    }

    TEST_ASSERT_EQUAL(1, found_ralph_glob);
    TEST_ASSERT_EQUAL(1, found_env_glob);
    TEST_ASSERT_EQUAL(1, found_ralph_dir_glob);
}

/* =============================================================================
 * Basename Protection Tests
 * ========================================================================== */

void test_is_protected_basename_ralph_config(void) {
    TEST_ASSERT_EQUAL(1, is_protected_basename("ralph.config.json"));
}

void test_is_protected_basename_env(void) {
    TEST_ASSERT_EQUAL(1, is_protected_basename(".env"));
}

void test_is_protected_basename_env_local(void) {
    /* .env.local matches .env. prefix */
    TEST_ASSERT_EQUAL(1, is_protected_basename(".env.local"));
}

void test_is_protected_basename_env_production(void) {
    TEST_ASSERT_EQUAL(1, is_protected_basename(".env.production"));
}

void test_is_protected_basename_env_development(void) {
    TEST_ASSERT_EQUAL(1, is_protected_basename(".env.development"));
}

void test_is_protected_basename_env_test(void) {
    TEST_ASSERT_EQUAL(1, is_protected_basename(".env.test"));
}

void test_is_protected_basename_not_protected(void) {
    TEST_ASSERT_EQUAL(0, is_protected_basename("main.c"));
    TEST_ASSERT_EQUAL(0, is_protected_basename("README.md"));
    TEST_ASSERT_EQUAL(0, is_protected_basename("config.json"));
    TEST_ASSERT_EQUAL(0, is_protected_basename("env.txt"));
}

void test_is_protected_basename_null(void) {
    TEST_ASSERT_EQUAL(0, is_protected_basename(NULL));
}

void test_is_protected_basename_empty(void) {
    TEST_ASSERT_EQUAL(0, is_protected_basename(""));
}

/* =============================================================================
 * Glob Pattern Matching Tests
 * ========================================================================== */

void test_matches_protected_glob_ralph_config_root(void) {
    TEST_ASSERT_EQUAL(1, matches_protected_glob("ralph.config.json"));
}

void test_matches_protected_glob_ralph_config_nested(void) {
    TEST_ASSERT_EQUAL(1, matches_protected_glob("project/ralph.config.json"));
    TEST_ASSERT_EQUAL(1, matches_protected_glob("/home/user/project/ralph.config.json"));
}

void test_matches_protected_glob_env_file(void) {
    TEST_ASSERT_EQUAL(1, matches_protected_glob(".env"));
    TEST_ASSERT_EQUAL(1, matches_protected_glob("project/.env"));
    TEST_ASSERT_EQUAL(1, matches_protected_glob("/home/user/project/.env"));
}

void test_matches_protected_glob_env_variants(void) {
    TEST_ASSERT_EQUAL(1, matches_protected_glob(".env.local"));
    TEST_ASSERT_EQUAL(1, matches_protected_glob("project/.env.production"));
    TEST_ASSERT_EQUAL(1, matches_protected_glob("/app/.env.development"));
}

void test_matches_protected_glob_ralph_dir_config(void) {
    TEST_ASSERT_EQUAL(1, matches_protected_glob(".ralph/config.json"));
    TEST_ASSERT_EQUAL(1, matches_protected_glob("/home/user/.ralph/config.json"));
}

void test_matches_protected_glob_not_protected(void) {
    TEST_ASSERT_EQUAL(0, matches_protected_glob("main.c"));
    TEST_ASSERT_EQUAL(0, matches_protected_glob("/home/user/config.json"));
    TEST_ASSERT_EQUAL(0, matches_protected_glob("project/settings.json"));
}

void test_matches_protected_glob_null(void) {
    TEST_ASSERT_EQUAL(0, matches_protected_glob(NULL));
}

void test_matches_protected_glob_empty(void) {
    TEST_ASSERT_EQUAL(0, matches_protected_glob(""));
}

/* =============================================================================
 * Full Path Protection Tests
 * ========================================================================== */

void test_is_protected_file_ralph_config(void) {
    TEST_ASSERT_EQUAL(1, is_protected_file("ralph.config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("./ralph.config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/project/ralph.config.json"));
}

void test_is_protected_file_env(void) {
    TEST_ASSERT_EQUAL(1, is_protected_file(".env"));
    TEST_ASSERT_EQUAL(1, is_protected_file("./.env"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/home/user/.env"));
}

void test_is_protected_file_env_variants(void) {
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.local"));
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.production"));
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.development"));
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.test"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/app/.env.staging"));
}

void test_is_protected_file_ralph_dir_config(void) {
    TEST_ASSERT_EQUAL(1, is_protected_file(".ralph/config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/home/user/.ralph/config.json"));
}

void test_is_protected_file_not_protected(void) {
    TEST_ASSERT_EQUAL(0, is_protected_file("main.c"));
    TEST_ASSERT_EQUAL(0, is_protected_file("./src/config.c"));
    TEST_ASSERT_EQUAL(0, is_protected_file("/etc/hosts"));
    TEST_ASSERT_EQUAL(0, is_protected_file("README.md"));
}

void test_is_protected_file_null(void) {
    TEST_ASSERT_EQUAL(0, is_protected_file(NULL));
}

void test_is_protected_file_empty(void) {
    TEST_ASSERT_EQUAL(0, is_protected_file(""));
}

/* =============================================================================
 * Inode Cache Tests
 * ========================================================================== */

void test_add_protected_inode_if_exists_nonexistent(void) {
    /* Adding a nonexistent file should not crash or add anything */
    add_protected_inode_if_exists("/nonexistent/path/to/file.txt");
    /* Just verifying it doesn't crash */
    TEST_ASSERT_TRUE(1);
}

void test_add_protected_inode_null(void) {
    /* Should handle NULL gracefully */
    add_protected_inode_if_exists(NULL);
    TEST_ASSERT_TRUE(1);
}

void test_clear_protected_inode_cache(void) {
    /* Clear should not crash even on empty cache */
    clear_protected_inode_cache();
    TEST_ASSERT_TRUE(1);
}

void test_inode_detection_for_actual_file(void) {
    /* Create a test file and verify inode-based detection */
    char path[512];
    memset(path, 0, sizeof(path)); /* Initialize to silence valgrind */
    snprintf(path, sizeof(path), "%s/.env", test_dir);

    /* Create the file */
    TEST_ASSERT_EQUAL(0, create_test_file(path));

    /* Initialize and scan - should find our test file */
    protected_files_init();
    add_protected_inode_if_exists(path);

    /* The file should be protected (by basename at least) */
    TEST_ASSERT_EQUAL(1, is_protected_file(path));

    /* Clean up */
    remove_test_file(path);
}

void test_inode_detection_after_refresh(void) {
    /* Create a test file */
    char path[512];
    memset(path, 0, sizeof(path)); /* Initialize to silence valgrind */
    snprintf(path, sizeof(path), "%s/ralph.config.json", test_dir);
    TEST_ASSERT_EQUAL(0, create_test_file(path));

    /* Force a refresh to pick up the new file */
    force_protected_inode_refresh();

    /* Add it explicitly */
    add_protected_inode_if_exists(path);

    /* Verify it's protected */
    TEST_ASSERT_EQUAL(1, is_protected_file(path));

    /* Clean up */
    remove_test_file(path);
}

/* =============================================================================
 * Initialization and Cleanup Tests
 * ========================================================================== */

void test_protected_files_init(void) {
    int result = protected_files_init();
    TEST_ASSERT_EQUAL(0, result);
}

void test_protected_files_double_init(void) {
    /* Double init should be safe */
    protected_files_init();
    int result = protected_files_init();
    TEST_ASSERT_EQUAL(0, result);
}

void test_protected_files_cleanup(void) {
    /* Cleanup should not crash */
    protected_files_cleanup();
    TEST_ASSERT_TRUE(1);
}

void test_protected_files_cleanup_double(void) {
    /* Double cleanup should be safe */
    protected_files_cleanup();
    protected_files_cleanup();
    TEST_ASSERT_TRUE(1);
}

void test_protected_files_init_cleanup_cycle(void) {
    /* Multiple init/cleanup cycles should be safe */
    for (int i = 0; i < 3; i++) {
        protected_files_init();
        protected_files_cleanup();
    }
    TEST_ASSERT_TRUE(1);
}

/* =============================================================================
 * Edge Cases
 * ========================================================================== */

void test_is_protected_file_similar_names(void) {
    /*
     * Files that look similar but should NOT be protected.
     * Note: .env.bak IS protected because it starts with .env. prefix
     * (backup files of sensitive data should also be protected).
     */
    TEST_ASSERT_EQUAL(0, is_protected_file("ralph.config.json.bak")); /* Suffix, not exact match */
    TEST_ASSERT_EQUAL(1, is_protected_file(".env.bak")); /* Starts with .env. - protected */
    TEST_ASSERT_EQUAL(0, is_protected_file("env")); /* Not .env */
    TEST_ASSERT_EQUAL(0, is_protected_file("ralph_config.json")); /* Underscore, not dot */
}

void test_is_protected_file_paths_with_protected_substring(void) {
    /* Path contains protected name but as directory, not file */
    TEST_ASSERT_EQUAL(0, is_protected_file(".env/config.json"));
    TEST_ASSERT_EQUAL(0, is_protected_file("ralph.config.json/subdir/file.txt"));
}

void test_is_protected_file_deep_paths(void) {
    /* Protected files in deep directory structures */
    TEST_ASSERT_EQUAL(1, is_protected_file("/a/b/c/d/e/f/g/.env"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/very/long/path/to/project/ralph.config.json"));
}

void test_is_protected_basename_config_json_not_protected(void) {
    /* Just "config.json" is not protected - needs .ralph/ parent */
    TEST_ASSERT_EQUAL(0, is_protected_basename("config.json"));
}

/* =============================================================================
 * Platform-Specific Tests (POSIX)
 * ========================================================================== */

#ifndef _WIN32
void test_is_protected_file_posix_case_sensitive(void) {
    /*
     * On POSIX, case matters for protected file detection.
     * Test with paths in a nonexistent directory to avoid inode cache matches.
     */
    TEST_ASSERT_EQUAL(0, is_protected_file("/nonexistent/dir/RALPH.CONFIG.JSON"));
    TEST_ASSERT_EQUAL(0, is_protected_file("/nonexistent/dir/.ENV"));
    TEST_ASSERT_EQUAL(0, is_protected_file("/nonexistent/dir/.Env"));

    /* Also verify the lowercase variants ARE protected */
    TEST_ASSERT_EQUAL(1, is_protected_file("/nonexistent/dir/ralph.config.json"));
    TEST_ASSERT_EQUAL(1, is_protected_file("/nonexistent/dir/.env"));
}
#endif

/* =============================================================================
 * Cleanup after all tests
 * ========================================================================== */

static void cleanup_test_directory(void) {
    if (test_dir_created) {
        char cmd[512];
        memset(cmd, 0, sizeof(cmd)); /* Initialize to silence valgrind */
        snprintf(cmd, sizeof(cmd), "rm -rf %s 2>/dev/null", test_dir);
        (void)system(cmd);
    }
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Pattern access tests */
    RUN_TEST(test_get_protected_basename_patterns_returns_array);
    RUN_TEST(test_get_protected_prefix_patterns_returns_array);
    RUN_TEST(test_get_protected_glob_patterns_returns_array);

    /* Basename protection tests */
    RUN_TEST(test_is_protected_basename_ralph_config);
    RUN_TEST(test_is_protected_basename_env);
    RUN_TEST(test_is_protected_basename_env_local);
    RUN_TEST(test_is_protected_basename_env_production);
    RUN_TEST(test_is_protected_basename_env_development);
    RUN_TEST(test_is_protected_basename_env_test);
    RUN_TEST(test_is_protected_basename_not_protected);
    RUN_TEST(test_is_protected_basename_null);
    RUN_TEST(test_is_protected_basename_empty);

    /* Glob pattern matching tests */
    RUN_TEST(test_matches_protected_glob_ralph_config_root);
    RUN_TEST(test_matches_protected_glob_ralph_config_nested);
    RUN_TEST(test_matches_protected_glob_env_file);
    RUN_TEST(test_matches_protected_glob_env_variants);
    RUN_TEST(test_matches_protected_glob_ralph_dir_config);
    RUN_TEST(test_matches_protected_glob_not_protected);
    RUN_TEST(test_matches_protected_glob_null);
    RUN_TEST(test_matches_protected_glob_empty);

    /* Full path protection tests */
    RUN_TEST(test_is_protected_file_ralph_config);
    RUN_TEST(test_is_protected_file_env);
    RUN_TEST(test_is_protected_file_env_variants);
    RUN_TEST(test_is_protected_file_ralph_dir_config);
    RUN_TEST(test_is_protected_file_not_protected);
    RUN_TEST(test_is_protected_file_null);
    RUN_TEST(test_is_protected_file_empty);

    /* Inode cache tests */
    RUN_TEST(test_add_protected_inode_if_exists_nonexistent);
    RUN_TEST(test_add_protected_inode_null);
    RUN_TEST(test_clear_protected_inode_cache);
    RUN_TEST(test_inode_detection_for_actual_file);
    RUN_TEST(test_inode_detection_after_refresh);

    /* Initialization and cleanup tests */
    RUN_TEST(test_protected_files_init);
    RUN_TEST(test_protected_files_double_init);
    RUN_TEST(test_protected_files_cleanup);
    RUN_TEST(test_protected_files_cleanup_double);
    RUN_TEST(test_protected_files_init_cleanup_cycle);

    /* Edge cases */
    RUN_TEST(test_is_protected_file_similar_names);
    RUN_TEST(test_is_protected_file_paths_with_protected_substring);
    RUN_TEST(test_is_protected_file_deep_paths);
    RUN_TEST(test_is_protected_basename_config_json_not_protected);

    /* Platform-specific tests */
#ifndef _WIN32
    RUN_TEST(test_is_protected_file_posix_case_sensitive);
#endif

    int result = UNITY_END();

    /* Cleanup test directory */
    cleanup_test_directory();

    return result;
}
