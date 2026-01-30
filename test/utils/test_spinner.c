#include "unity.h"
#include "spinner.h"
#include "output_formatter.h"
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
    return UNITY_END();
}
