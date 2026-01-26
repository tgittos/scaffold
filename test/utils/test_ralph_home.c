#include "../unity/unity.h"
#include "../../src/utils/ralph_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char original_cwd[1024];
static char *original_home = NULL;
static char *original_ralph_home = NULL;

void setUp(void) {
    // Save original environment
    getcwd(original_cwd, sizeof(original_cwd));
    const char *home = getenv("HOME");
    if (home) {
        original_home = strdup(home);
    }
    const char *ralph_home = getenv("RALPH_HOME");
    if (ralph_home) {
        original_ralph_home = strdup(ralph_home);
    }

    // Clean up any existing ralph_home state
    ralph_home_cleanup();
}

void tearDown(void) {
    // Cleanup ralph_home
    ralph_home_cleanup();

    // Restore original environment
    chdir(original_cwd);
    if (original_home) {
        setenv("HOME", original_home, 1);
        free(original_home);
        original_home = NULL;
    }
    if (original_ralph_home) {
        setenv("RALPH_HOME", original_ralph_home, 1);
        free(original_ralph_home);
        original_ralph_home = NULL;
    } else {
        unsetenv("RALPH_HOME");
    }
}

/* Test initialization with no overrides (default path) */
void test_init_default_path(void) {
    // Ensure no RALPH_HOME env var
    unsetenv("RALPH_HOME");

    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(NULL));
    TEST_ASSERT_EQUAL_INT(1, ralph_home_is_initialized());

    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);

    // Should end with .local/ralph
    const char *suffix = "/.local/ralph";
    size_t home_len = strlen(home);
    size_t suffix_len = strlen(suffix);
    TEST_ASSERT_TRUE(home_len > suffix_len);
    TEST_ASSERT_EQUAL_STRING(suffix, home + home_len - suffix_len);
}

/* Test initialization with CLI override */
void test_init_cli_override(void) {
    // Set RALPH_HOME to verify CLI takes priority
    setenv("RALPH_HOME", "/env/path", 1);

    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/cli/path"));

    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("/cli/path", home);
}

/* Test initialization with environment variable */
void test_init_env_var(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(NULL));

    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("/env/path", home);
}

/* Test priority: CLI > env > default */
void test_init_priority(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    // CLI override should take priority
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/cli/path"));
    TEST_ASSERT_EQUAL_STRING("/cli/path", ralph_home_get());

    ralph_home_cleanup();

    // With no CLI override, env should be used
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(NULL));
    TEST_ASSERT_EQUAL_STRING("/env/path", ralph_home_get());

    ralph_home_cleanup();
    unsetenv("RALPH_HOME");

    // With no CLI or env, default should be used
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(NULL));
    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_TRUE(strstr(home, ".local/ralph") != NULL);
}

/* Test relative path resolution */
void test_init_relative_path(void) {
    // Use a relative path
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(".ralph"));

    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);

    // Should be an absolute path starting with /
    TEST_ASSERT_EQUAL_CHAR('/', home[0]);

    // Should contain our relative path component
    TEST_ASSERT_TRUE(strstr(home, ".ralph") != NULL);
}

/* Test relative path with ./ prefix */
void test_init_relative_path_dot_slash(void) {
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("./.ralph"));

    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);

    // Should be an absolute path starting with /
    TEST_ASSERT_EQUAL_CHAR('/', home[0]);

    // Should not contain ./
    TEST_ASSERT_NULL(strstr(home, "./"));
}

/* Test ralph_home_path concatenation */
void test_ralph_home_path(void) {
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/test/home"));

    char *path = ralph_home_path("tasks.db");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("/test/home/tasks.db", path);
    free(path);

    // Test with leading slash in relative path (should be stripped)
    path = ralph_home_path("/config.json");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("/test/home/config.json", path);
    free(path);

    // Test with subdirectory
    path = ralph_home_path("data/vectors");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("/test/home/data/vectors", path);
    free(path);
}

/* Test ralph_home_path with NULL input */
void test_ralph_home_path_null(void) {
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/test/home"));
    TEST_ASSERT_NULL(ralph_home_path(NULL));
}

/* Test ralph_home_path before initialization */
void test_ralph_home_path_not_initialized(void) {
    // Don't call init
    TEST_ASSERT_NULL(ralph_home_path("test.db"));
}

/* Test ralph_home_get before initialization */
void test_ralph_home_get_not_initialized(void) {
    TEST_ASSERT_NULL(ralph_home_get());
    TEST_ASSERT_EQUAL_INT(0, ralph_home_is_initialized());
}

/* Test ensure_exists creates directory */
void test_ensure_exists(void) {
    // Use a temp directory path
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/ralph_test_%d", getpid());

    // Make sure it doesn't exist
    rmdir(temp_path);

    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(temp_path));
    TEST_ASSERT_EQUAL_INT(0, ralph_home_ensure_exists());

    // Verify directory exists
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(temp_path, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    // Cleanup
    rmdir(temp_path);
}

/* Test cleanup */
void test_cleanup(void) {
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/test/path"));
    TEST_ASSERT_EQUAL_INT(1, ralph_home_is_initialized());
    TEST_ASSERT_NOT_NULL(ralph_home_get());

    ralph_home_cleanup();

    TEST_ASSERT_EQUAL_INT(0, ralph_home_is_initialized());
    TEST_ASSERT_NULL(ralph_home_get());
}

/* Test reinitialization */
void test_reinit(void) {
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/first/path"));
    TEST_ASSERT_EQUAL_STRING("/first/path", ralph_home_get());

    // Reinitialize with different path
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init("/second/path"));
    TEST_ASSERT_EQUAL_STRING("/second/path", ralph_home_get());
}

/* Test empty string override */
void test_empty_string_override(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    // Empty string should be treated as no override
    TEST_ASSERT_EQUAL_INT(0, ralph_home_init(""));

    const char *home = ralph_home_get();
    TEST_ASSERT_NOT_NULL(home);
    // Should fall back to env var
    TEST_ASSERT_EQUAL_STRING("/env/path", home);
}

int main(void) {
    UNITY_BEGIN();

    /* Default initialization */
    RUN_TEST(test_init_default_path);

    /* Override tests */
    RUN_TEST(test_init_cli_override);
    RUN_TEST(test_init_env_var);
    RUN_TEST(test_init_priority);

    /* Relative path tests */
    RUN_TEST(test_init_relative_path);
    RUN_TEST(test_init_relative_path_dot_slash);

    /* Path building tests */
    RUN_TEST(test_ralph_home_path);
    RUN_TEST(test_ralph_home_path_null);
    RUN_TEST(test_ralph_home_path_not_initialized);

    /* Get before init */
    RUN_TEST(test_ralph_home_get_not_initialized);

    /* Directory creation */
    RUN_TEST(test_ensure_exists);

    /* Cleanup tests */
    RUN_TEST(test_cleanup);
    RUN_TEST(test_reinit);
    RUN_TEST(test_empty_string_override);

    return UNITY_END();
}
