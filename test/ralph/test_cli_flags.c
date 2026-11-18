#include "unity/unity.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include "build/version.h"

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
    char *output = run_command("./out/scaffold --version 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "scaffold"));
    TEST_ASSERT_NOT_NULL(strstr(output, RALPH_VERSION));
}

void test_version_short_flag(void) {
    char *output = run_command("./out/scaffold -v 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "scaffold"));
    TEST_ASSERT_NOT_NULL(strstr(output, RALPH_VERSION));
}

void test_help_long_flag(void) {
    char *output = run_command("./out/scaffold --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "scaffold"));
    TEST_ASSERT_NOT_NULL(strstr(output, RALPH_VERSION));
    TEST_ASSERT_NOT_NULL(strstr(output, "Usage:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Options:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--help"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--version"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--debug"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--no-stream"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--json"));
    TEST_ASSERT_NOT_NULL(strstr(output, "--home"));
}

void test_help_short_flag(void) {
    char *output = run_command("./out/scaffold -h 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "scaffold"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Usage:"));
    TEST_ASSERT_NOT_NULL(strstr(output, "Options:"));
}

void test_help_excludes_internal_flags(void) {
    char *output = run_command("./out/scaffold --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    // Internal flags should NOT be documented in user help
    TEST_ASSERT_NULL(strstr(output, "--subagent"));
    TEST_ASSERT_NULL(strstr(output, "--task"));
    TEST_ASSERT_NULL(strstr(output, "--context "));
    TEST_ASSERT_NULL(strstr(output, "--worker"));
    TEST_ASSERT_NULL(strstr(output, "--queue"));
    TEST_ASSERT_NULL(strstr(output, "--no-auto-messages"));
    TEST_ASSERT_NULL(strstr(output, "--message-poll-interval"));
}

void test_version_exits_immediately(void) {
    // Version flag should exit with success (0)
    int ret = system("./out/scaffold --version > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_help_exits_immediately(void) {
    // Help flag should exit with success (0)
    int ret = system("./out/scaffold --help > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_unknown_flag_errors(void) {
    char *output = run_command("./out/scaffold --bogus 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "unknown option '--bogus'"));
    int ret = system("./out/scaffold --bogus > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(2, WEXITSTATUS(ret));
}

void test_flag_missing_value_errors(void) {
    char *output = run_command("./out/scaffold --model 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "--model requires a value"));
    int ret = system("./out/scaffold --model > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(2, WEXITSTATUS(ret));
}

void test_help_lists_model_flag(void) {
    char *output = run_command("./out/scaffold --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "--model <name>"));
}

void test_help_lists_slash_commands(void) {
    char *output = run_command("./out/scaffold --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "/goals"));
    TEST_ASSERT_NOT_NULL(strstr(output, "/model"));
}

void test_help_tagline(void) {
    char *output = run_command("./out/scaffold --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "autonomous coding agent"));
    TEST_ASSERT_NULL(strstr(output, "AI Orchestrator"));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_version_long_flag);
    RUN_TEST(test_version_short_flag);
    RUN_TEST(test_help_long_flag);
    RUN_TEST(test_help_short_flag);
    RUN_TEST(test_help_excludes_internal_flags);
    RUN_TEST(test_version_exits_immediately);
    RUN_TEST(test_help_exits_immediately);
    RUN_TEST(test_unknown_flag_errors);
    RUN_TEST(test_flag_missing_value_errors);
    RUN_TEST(test_help_lists_model_flag);
    RUN_TEST(test_help_lists_slash_commands);
    RUN_TEST(test_help_tagline);

    return UNITY_END();
}
