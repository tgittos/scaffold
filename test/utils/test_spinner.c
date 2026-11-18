#include "unity.h"
#include "ui/spinner.h"
#include "ui/output_formatter.h"
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>

// Redirect stdout to /dev/null to prevent ANSI escape codes from corrupting output streams
static int original_stdout = -1;
static int devnull_fd = -1;

static void redirect_stdout_to_devnull(void) {
    if (original_stdout == -1) {
        fflush(stdout);
        original_stdout = dup(STDOUT_FILENO);
        devnull_fd = open("/dev/null", O_WRONLY);
        if (devnull_fd != -1) {
            dup2(devnull_fd, STDOUT_FILENO);
        }
    }
}

static void restore_stdout(void) {
    if (original_stdout != -1) {
        fflush(stdout);
        dup2(original_stdout, STDOUT_FILENO);
        close(original_stdout);
        original_stdout = -1;
    }
    if (devnull_fd != -1) {
        close(devnull_fd);
        devnull_fd = -1;
    }
}

void setUp(void) {
    set_json_output_mode(false);
    // Redirect stdout for spinner tests that actually run the spinner
    // to prevent ANSI escape codes from corrupting output streams
    redirect_stdout_to_devnull();
}

void tearDown(void) {
    spinner_cleanup();
    restore_stdout();
}

void test_spinner_start_stop_basic(void) {
    spinner_start("shell", "{\"command\": \"ls\"}");
    usleep(100000);  // 100ms - enough for at least one pulse
    spinner_stop();
    TEST_PASS();
}

void test_spinner_start_stop_no_arguments(void) {
    spinner_start("read_file", NULL);
    usleep(50000);  // 50ms
    spinner_stop();
    TEST_PASS();
}

void test_spinner_start_stop_empty_arguments(void) {
    spinner_start("list_files", "{}");
    usleep(50000);  // 50ms
    spinner_stop();
    TEST_PASS();
}

void test_spinner_stop_without_start(void) {
    spinner_stop();  // Should not crash
    TEST_PASS();
}

void test_spinner_cleanup_without_start(void) {
    spinner_cleanup();  // Should not crash
    TEST_PASS();
}

void test_spinner_double_stop(void) {
    spinner_start("shell", "{\"command\": \"pwd\"}");
    usleep(50000);
    spinner_stop();
    spinner_stop();  // Second stop should be safe
    TEST_PASS();
}

void test_spinner_json_mode_noop(void) {
    set_json_output_mode(true);
    spinner_start("shell", "{\"command\": \"ls\"}");  // Should be no-op
    usleep(50000);
    spinner_stop();  // Should not crash
    set_json_output_mode(false);
    TEST_PASS();
}

void test_spinner_start_while_running(void) {
    spinner_start("shell", "{\"command\": \"ls\"}");
    usleep(50000);
    spinner_start("read_file", "{\"path\": \"/tmp\"}");  // Should be ignored
    usleep(50000);
    spinner_stop();
    TEST_PASS();
}

void test_spinner_null_tool_name(void) {
    spinner_start(NULL, "{\"command\": \"ls\"}");
    usleep(50000);
    spinner_stop();
    TEST_PASS();
}

/* ---------------------------------------------------------------------------
 * New tests for elapsed time and TTY gating.
 * ---------------------------------------------------------------------------
 */

static time_t g_fake_time = 0;
static time_t fake_clock(void) { return g_fake_time; }

void test_spinner_elapsed(void) {
    /* Set clock to report 0 at start, then 3 seconds later. */
    g_fake_time = 0;
    spinner_set_clock_for_test(fake_clock);

    char frame[512];
    /* Build a frame with start_time=0, current clock=3 (3s elapsed). */
    g_fake_time = 3;
    spinner_build_frame_for_test(frame, sizeof(frame), "shell", NULL, 0);

    spinner_set_clock_for_test(NULL); /* restore */

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(frame, "(3s)"),
        "Spinner frame must contain \"(3s)\" after 3 seconds elapsed");
}

void test_no_ansi_when_not_tty(void) {
    /* setUp already redirected stdout to /dev/null, which is not a TTY.
     * spinner_start() must be a complete no-op on non-TTY stdout. */
    spinner_start("test_tool", NULL);
    int running = spinner_is_running_for_test();
    /* spinner_stop() must not crash even though the spinner never started. */
    spinner_stop();
    TEST_ASSERT_FALSE_MESSAGE(running,
        "Spinner must not start (and emit no ANSI codes) when stdout is not a TTY");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_spinner_start_stop_basic);
    RUN_TEST(test_spinner_start_stop_no_arguments);
    RUN_TEST(test_spinner_start_stop_empty_arguments);
    RUN_TEST(test_spinner_stop_without_start);
    RUN_TEST(test_spinner_cleanup_without_start);
    RUN_TEST(test_spinner_double_stop);
    RUN_TEST(test_spinner_json_mode_noop);
    RUN_TEST(test_spinner_start_while_running);
    RUN_TEST(test_spinner_null_tool_name);
    RUN_TEST(test_spinner_elapsed);
    RUN_TEST(test_no_ansi_when_not_tty);
    return UNITY_END();
}
