#include "../unity/unity.h"
#include "util/app_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char original_cwd[1024];
static char *original_home = NULL;
static char *original_ralph_home = NULL;
static char *original_scaffold_home = NULL;

void setUp(void) {
    getcwd(original_cwd, sizeof(original_cwd));
    const char *home = getenv("HOME");
    if (home) {
        original_home = strdup(home);
    }
    const char *ralph_home = getenv("RALPH_HOME");
    if (ralph_home) {
        original_ralph_home = strdup(ralph_home);
    }
    const char *scaffold_home = getenv("SCAFFOLD_HOME");
    if (scaffold_home) {
        original_scaffold_home = strdup(scaffold_home);
    }

    app_home_cleanup();
}

void tearDown(void) {
    app_home_cleanup();

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
    if (original_scaffold_home) {
        setenv("SCAFFOLD_HOME", original_scaffold_home, 1);
        free(original_scaffold_home);
        original_scaffold_home = NULL;
    } else {
        unsetenv("SCAFFOLD_HOME");
    }
}

/* Test initialization with no overrides (default path) - defaults to "ralph" */
void test_init_default_path(void) {
    unsetenv("RALPH_HOME");

    TEST_ASSERT_EQUAL_INT(0, app_home_init(NULL));
    TEST_ASSERT_EQUAL_INT(1, app_home_is_initialized());

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);

    const char *suffix = "/.local/ralph";
    size_t home_len = strlen(home);
    size_t suffix_len = strlen(suffix);
    TEST_ASSERT_TRUE(home_len > suffix_len);
    TEST_ASSERT_EQUAL_STRING(suffix, home + home_len - suffix_len);
}

/* Test initialization with CLI override */
void test_init_cli_override(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    TEST_ASSERT_EQUAL_INT(0, app_home_init("/cli/path"));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("/cli/path", home);
}

/* Test initialization with environment variable */
void test_init_env_var(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    TEST_ASSERT_EQUAL_INT(0, app_home_init(NULL));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("/env/path", home);
}

/* Test priority: CLI > env > default */
void test_init_priority(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    TEST_ASSERT_EQUAL_INT(0, app_home_init("/cli/path"));
    TEST_ASSERT_EQUAL_STRING("/cli/path", app_home_get());

    app_home_cleanup();

    TEST_ASSERT_EQUAL_INT(0, app_home_init(NULL));
    TEST_ASSERT_EQUAL_STRING("/env/path", app_home_get());

    app_home_cleanup();
    unsetenv("RALPH_HOME");

    TEST_ASSERT_EQUAL_INT(0, app_home_init(NULL));
    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_TRUE(strstr(home, ".local/ralph") != NULL);
}

/* Test relative path resolution */
void test_init_relative_path(void) {
    TEST_ASSERT_EQUAL_INT(0, app_home_init(".ralph"));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);

    TEST_ASSERT_EQUAL_CHAR('/', home[0]);
    TEST_ASSERT_TRUE(strstr(home, ".ralph") != NULL);
}

/* Test relative path with ./ prefix */
void test_init_relative_path_dot_slash(void) {
    TEST_ASSERT_EQUAL_INT(0, app_home_init("./.ralph"));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);

    TEST_ASSERT_EQUAL_CHAR('/', home[0]);
    TEST_ASSERT_NULL(strstr(home, "./"));
}

/* Test app_home_path concatenation */
void test_app_home_path(void) {
    TEST_ASSERT_EQUAL_INT(0, app_home_init("/test/home"));

    char *path = app_home_path("tasks.db");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("/test/home/tasks.db", path);
    free(path);

    path = app_home_path("/config.json");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("/test/home/config.json", path);
    free(path);

    path = app_home_path("data/vectors");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_EQUAL_STRING("/test/home/data/vectors", path);
    free(path);
}

/* Test app_home_path with NULL input */
void test_app_home_path_null(void) {
    TEST_ASSERT_EQUAL_INT(0, app_home_init("/test/home"));
    TEST_ASSERT_NULL(app_home_path(NULL));
}

/* Test app_home_path before initialization */
void test_app_home_path_not_initialized(void) {
    TEST_ASSERT_NULL(app_home_path("test.db"));
}

/* Test app_home_get before initialization */
void test_app_home_get_not_initialized(void) {
    TEST_ASSERT_NULL(app_home_get());
    TEST_ASSERT_EQUAL_INT(0, app_home_is_initialized());
}

/* Test ensure_exists creates directory */
void test_ensure_exists(void) {
    char temp_path[256];
    snprintf(temp_path, sizeof(temp_path), "/tmp/ralph_test_%d", getpid());

    rmdir(temp_path);

    TEST_ASSERT_EQUAL_INT(0, app_home_init(temp_path));
    TEST_ASSERT_EQUAL_INT(0, app_home_ensure_exists());

    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(temp_path, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));

    rmdir(temp_path);
}

/* Test cleanup */
void test_cleanup(void) {
    TEST_ASSERT_EQUAL_INT(0, app_home_init("/test/path"));
    TEST_ASSERT_EQUAL_INT(1, app_home_is_initialized());
    TEST_ASSERT_NOT_NULL(app_home_get());

    app_home_cleanup();

    TEST_ASSERT_EQUAL_INT(0, app_home_is_initialized());
    TEST_ASSERT_NULL(app_home_get());
}

/* Test reinitialization */
void test_reinit(void) {
    TEST_ASSERT_EQUAL_INT(0, app_home_init("/first/path"));
    TEST_ASSERT_EQUAL_STRING("/first/path", app_home_get());

    TEST_ASSERT_EQUAL_INT(0, app_home_init("/second/path"));
    TEST_ASSERT_EQUAL_STRING("/second/path", app_home_get());
}

/* Test empty string override */
void test_empty_string_override(void) {
    setenv("RALPH_HOME", "/env/path", 1);

    TEST_ASSERT_EQUAL_INT(0, app_home_init(""));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("/env/path", home);
}

/* Test custom app name changes default directory and env var */
void test_custom_app_name(void) {
    unsetenv("RALPH_HOME");
    unsetenv("SCAFFOLD_HOME");

    app_home_set_app_name("scaffold");
    TEST_ASSERT_EQUAL_INT(0, app_home_init(NULL));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);

    const char *suffix = "/.local/scaffold";
    size_t home_len = strlen(home);
    size_t suffix_len = strlen(suffix);
    TEST_ASSERT_TRUE(home_len > suffix_len);
    TEST_ASSERT_EQUAL_STRING(suffix, home + home_len - suffix_len);
}

/* Test custom app name reads correct env var */
void test_custom_app_name_env_var(void) {
    setenv("SCAFFOLD_HOME", "/scaffold/env", 1);

    app_home_set_app_name("scaffold");
    TEST_ASSERT_EQUAL_INT(0, app_home_init(NULL));

    const char *home = app_home_get();
    TEST_ASSERT_NOT_NULL(home);
    TEST_ASSERT_EQUAL_STRING("/scaffold/env", home);

    unsetenv("SCAFFOLD_HOME");
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
    RUN_TEST(test_app_home_path);
    RUN_TEST(test_app_home_path_null);
    RUN_TEST(test_app_home_path_not_initialized);

    /* Get before init */
    RUN_TEST(test_app_home_get_not_initialized);

    /* Directory creation */
    RUN_TEST(test_ensure_exists);

    /* Cleanup tests */
    RUN_TEST(test_cleanup);
    RUN_TEST(test_reinit);
    RUN_TEST(test_empty_string_override);

    /* Custom app name tests */
    RUN_TEST(test_custom_app_name);
    RUN_TEST(test_custom_app_name_env_var);

    return UNITY_END();
}
