#include "unity/unity.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RALPH_VERSION "0.1.0"

void setUp(void) {}

void tearDown(void) {}

static char* run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        return NULL;
    }

    static char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    size_t total = 0;
    while (fgets(buffer + total, sizeof(buffer) - total, fp) != NULL) {
        total = strlen(buffer);
        if (total >= sizeof(buffer) - 1) break;
    }

    pclose(fp);
    return buffer;
}

void test_version_long_flag(void) {
    char *output = run_command("./ralph --version 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "ralph"));
    TEST_ASSERT_NOT_NULL(strstr(output, RALPH_VERSION));
}

void test_version_short_flag(void) {
    char *output = run_command("./ralph -v 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "ralph"));
    TEST_ASSERT_NOT_NULL(strstr(output, RALPH_VERSION));
}

void test_help_long_flag(void) {
    char *output = run_command("./ralph --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "ralph"));
    TEST_ASSERT_NOT_NULL(strstr(output, RALPH_VERSION));
    TEST_ASSERT_NOT_NULL(strstr(output, "Usage:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Options:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--help"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--version"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--debug"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--no-stream"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--json"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--home"));
    // Approval gate CLI flags
    TEST_ASSERT_NOT_NULL(strstr(output, "--yolo"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--allow"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--allow-category"));
}

void test_help_short_flag(void) {
    char *output = run_command("./ralph -h 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "ralph"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Usage:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Options:"));
}

void test_help_excludes_subagent_flags(void) {
    char *output = run_command("./ralph --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    // Subagent flags should NOT be documented
    TEST_ASSERT_NULL(strstr(output, "--subagent"));
    TEST_ASSERT_NULL(strstr(output, "--task"));
    TEST_ASSERT_NULL(strstr(output, "--context"));
}

void test_version_exits_immediately(void) {
    // Version flag should exit with success (0)
    int ret = system("./ralph --version > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_help_exits_immediately(void) {
    // Help flag should exit with success (0)
    int ret = system("./ralph --help > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(0, ret);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_version_long_flag);
    RUN_TEST(test_version_short_flag);
    RUN_TEST(test_help_long_flag);
    RUN_TEST(test_help_short_flag);
    RUN_TEST(test_help_excludes_subagent_flags);
    RUN_TEST(test_version_exits_immediately);
    RUN_TEST(test_help_exits_immediately);

    return UNITY_END();
}
