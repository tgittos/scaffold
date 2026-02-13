#include "unity.h"
#include "orchestrator/role_prompts.h"
#include "util/app_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char test_home[256] = {0};

void setUp(void) {
    snprintf(test_home, sizeof(test_home), "/tmp/test_role_prompts_%d", getpid());
    mkdir(test_home, 0755);
    app_home_set_app_name("test_role");
    app_home_init(test_home);
}

void tearDown(void) {
    /* Clean up any prompt files */
    char path[512];
    snprintf(path, sizeof(path), "%s/prompts", test_home);

    /* Remove known test files */
    char file[768];
    snprintf(file, sizeof(file), "%s/custom_role.md", path);
    unlink(file);
    snprintf(file, sizeof(file), "%s/implementation.md", path);
    unlink(file);
    rmdir(path);
    rmdir(test_home);
    app_home_cleanup();
}

/* ========================================================================
 * role_prompt_builtin tests
 * ======================================================================== */

void test_builtin_implementation(void) {
    const char *p = role_prompt_builtin("implementation");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "implementation") != NULL);
    TEST_ASSERT_TRUE(strstr(p, "build") != NULL || strstr(p, "create") != NULL);
}

void test_builtin_code_review(void) {
    const char *p = role_prompt_builtin("code_review");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "review") != NULL || strstr(p, "Review") != NULL);
    TEST_ASSERT_TRUE(strstr(p, "security") != NULL);
}

void test_builtin_architecture_review(void) {
    const char *p = role_prompt_builtin("architecture_review");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "architecture") != NULL || strstr(p, "structural") != NULL);
}

void test_builtin_design_review(void) {
    const char *p = role_prompt_builtin("design_review");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "design") != NULL || strstr(p, "UX") != NULL);
}

void test_builtin_pm_review(void) {
    const char *p = role_prompt_builtin("pm_review");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "requirements") != NULL);
}

void test_builtin_testing(void) {
    const char *p = role_prompt_builtin("testing");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "test") != NULL || strstr(p, "Test") != NULL);
}

void test_builtin_unknown_role(void) {
    const char *p = role_prompt_builtin("some_unknown_role_xyz");
    TEST_ASSERT_NOT_NULL(p);
    /* Should return the generic worker prompt */
    TEST_ASSERT_TRUE(strstr(p, "worker") != NULL);
}

void test_builtin_null_role(void) {
    const char *p = role_prompt_builtin(NULL);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "worker") != NULL);
}

void test_builtin_empty_role(void) {
    const char *p = role_prompt_builtin("");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "worker") != NULL);
}

/* ========================================================================
 * role_prompt_load tests (file override)
 * ======================================================================== */

void test_load_returns_builtin_when_no_file(void) {
    char *p = role_prompt_load("implementation");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING(role_prompt_builtin("implementation"), p);
    free(p);
}

void test_load_returns_generic_for_unknown(void) {
    char *p = role_prompt_load("nonexistent_role");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING(role_prompt_builtin(NULL), p);
    free(p);
}

void test_load_returns_generic_for_null(void) {
    char *p = role_prompt_load(NULL);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING(role_prompt_builtin(NULL), p);
    free(p);
}

void test_load_file_override(void) {
    /* Create prompts directory and a custom file */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/prompts", test_home);
    mkdir(dir, 0755);

    char path[768];
    snprintf(path, sizeof(path), "%s/custom_role.md", dir);
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "Custom prompt for custom_role agent.");
    fclose(f);

    char *p = role_prompt_load("custom_role");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("Custom prompt for custom_role agent.", p);
    free(p);
}

void test_load_file_overrides_builtin(void) {
    /* A file for a known role should override the built-in */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/prompts", test_home);
    mkdir(dir, 0755);

    char path[768];
    snprintf(path, sizeof(path), "%s/implementation.md", dir);
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "Overridden implementation prompt.");
    fclose(f);

    char *p = role_prompt_load("implementation");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("Overridden implementation prompt.", p);
    free(p);
}

void test_load_trims_trailing_whitespace(void) {
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/prompts", test_home);
    mkdir(dir, 0755);

    char path[768];
    snprintf(path, sizeof(path), "%s/custom_role.md", dir);
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "Trimmed prompt.\n\n  \n");
    fclose(f);

    char *p = role_prompt_load("custom_role");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("Trimmed prompt.", p);
    free(p);
}

void test_load_rejects_unsafe_role_names(void) {
    /* Roles with path traversal chars should not try file loading */
    char *p = role_prompt_load("../etc/passwd");
    TEST_ASSERT_NOT_NULL(p);
    /* Should return generic (never tried to load file) */
    TEST_ASSERT_EQUAL_STRING(role_prompt_builtin(NULL), p);
    free(p);
}

void test_load_whitespace_only_file_falls_back(void) {
    /* An all-whitespace file should be treated as nonexistent */
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/prompts", test_home);
    mkdir(dir, 0755);

    char path[768];
    snprintf(path, sizeof(path), "%s/implementation.md", dir);
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "  \n\n\t\n  ");
    fclose(f);

    char *p = role_prompt_load("implementation");
    TEST_ASSERT_NOT_NULL(p);
    /* Should fall back to built-in, not return empty string */
    TEST_ASSERT_EQUAL_STRING(role_prompt_builtin("implementation"), p);
    free(p);
}

/* ========================================================================
 * Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Built-in prompt tests */
    RUN_TEST(test_builtin_implementation);
    RUN_TEST(test_builtin_code_review);
    RUN_TEST(test_builtin_architecture_review);
    RUN_TEST(test_builtin_design_review);
    RUN_TEST(test_builtin_pm_review);
    RUN_TEST(test_builtin_testing);
    RUN_TEST(test_builtin_unknown_role);
    RUN_TEST(test_builtin_null_role);
    RUN_TEST(test_builtin_empty_role);

    /* File-based loading tests */
    RUN_TEST(test_load_returns_builtin_when_no_file);
    RUN_TEST(test_load_returns_generic_for_unknown);
    RUN_TEST(test_load_returns_generic_for_null);
    RUN_TEST(test_load_file_override);
    RUN_TEST(test_load_file_overrides_builtin);
    RUN_TEST(test_load_trims_trailing_whitespace);
    RUN_TEST(test_load_rejects_unsafe_role_names);
    RUN_TEST(test_load_whitespace_only_file_falls_back);

    return UNITY_END();
}
