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
    TEST_ASSERT_NOT_NULL(strstr(output, "/recap"));
}

void test_help_tagline(void) {
    char *output = run_command("./out/scaffold --help 2>&1");
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_NOT_NULL(strstr(output, "autonomous coding agent"));
    TEST_ASSERT_NULL(strstr(output, "AI Orchestrator"));
}

void test_empty_stdin_errors(void) {
    /* Empty stdin with no message arg should exit 2 and print error to stderr */
    char *stderr_output = run_command("./out/scaffold < /dev/null 2>&1");
    TEST_ASSERT_NOT_NULL(stderr_output);
    TEST_ASSERT_NOT_NULL(strstr(stderr_output, "no input on stdin"));
    int ret = system("./out/scaffold < /dev/null > /dev/null 2>&1");
    TEST_ASSERT_EQUAL_INT(2, WEXITSTATUS(ret));
}

void test_json_interactive_no_prompt_on_stdout(void) {
    /*
     * Run the interactive REPL in --json mode with a real PTY stdin (via
     * script(1)) so isatty(STDIN_FILENO) is true and main enters REPL mode.
     * scaffold's stdout and stderr are redirected to separate files so they
     * can be inspected independently of the PTY session.  "timeout 5" ensures
     * the test cannot hang.
     *
     * Asserts:
     *   1. REPL ran: stderr contains "Goodbye" (sent there by repl.c in
     *      json mode).
     *   2. JSON purity: every non-empty stdout line starts with '{' or '['.
     */
    const char *out_path = "/tmp/scaffold_json_test_stdout.txt";
    const char *err_path = "/tmp/scaffold_json_test_stderr.txt";
    unlink(out_path);
    unlink(err_path);

    /* script -qec 'CMD' /dev/null runs CMD inside a PTY; the piped input
     * from the outer subshell is forwarded by script to the PTY master.
     * scaffold reads from the PTY slave (isatty == true -> REPL mode).
     * scaffold's own stdout/stderr are redirected to files, bypassing the
     * PTY, so they can be checked for content independently. */
    /* Send /help then quit (with retries) so the REPL receives both
     * regardless of startup latency.  /help exercises handler stdout
     * suppression in --json mode; quit terminates the session.
     * The outer timeout 20 is the hard ceiling. */
    int ret = system(
        "sh -c 'sleep 1; echo /help; sleep 1; echo quit; sleep 2; echo quit; sleep 2; echo quit' | "
        "timeout 20 script -qec "
        "'./out/scaffold --json"
        " >/tmp/scaffold_json_test_stdout.txt"
        " 2>/tmp/scaffold_json_test_stderr.txt' "
        "/dev/null >/dev/null 2>&1"
    );
    (void)ret;

    /* 1. Verify the REPL ran: stderr must contain "Goodbye" */
    FILE *err_fp = fopen(err_path, "r");
    TEST_ASSERT_NOT_NULL_MESSAGE(err_fp,
        "stderr file not created; REPL may not have started");
    char err_buf[4096] = {0};
    size_t err_pos = 0;
    char tmp_line[256];
    while (err_pos < sizeof(err_buf) - 1 &&
           fgets(tmp_line, sizeof(tmp_line), err_fp) != NULL) {
        size_t n = strlen(tmp_line);
        if (err_pos + n >= sizeof(err_buf) - 1) n = sizeof(err_buf) - 1 - err_pos;
        memcpy(err_buf + err_pos, tmp_line, n);
        err_pos += n;
    }
    fclose(err_fp);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(err_buf, "Goodbye"),
        "REPL did not send 'Goodbye' to stderr; did not enter interactive mode");

    /* 2. Verify JSON purity: every non-empty stdout line must start with
     * '{' or '['.  Empty lines (e.g. from readline's \n) are allowed. */
    FILE *out_fp = fopen(out_path, "r");
    if (out_fp == NULL) return;  /* no stdout output at all is acceptable */

    char line[4096];
    int found_non_json = 0;
    char bad_line[4096] = {0};
    while (fgets(line, sizeof(line), out_fp) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;
        if (line[0] != '{' && line[0] != '[') {
            found_non_json = 1;
            memcpy(bad_line, line, len < sizeof(bad_line)-1 ? len : sizeof(bad_line)-1);
            break;
        }
    }
    fclose(out_fp);

    if (found_non_json) {
        char msg[5120];
        snprintf(msg, sizeof(msg),
                 "--json stdout must contain only JSON lines; got: %.256s", bad_line);
        TEST_FAIL_MESSAGE(msg);
    }
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
    RUN_TEST(test_empty_stdin_errors);
    RUN_TEST(test_json_interactive_no_prompt_on_stdout);

    return UNITY_END();
}
