#include "unity.h"
#include "policy/child_mode.h"
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void setUp(void) { child_mode_set_process_gate(NULL); }
void tearDown(void) { child_mode_set_process_gate(NULL); }

void test_normal_mode_has_no_flag(void) {
    ApprovalGateConfig cfg = {.enabled = 1, .auto_mode = 0};
    const char *out[CHILD_MODE_FLAGS_MAX];
    TEST_ASSERT_EQUAL_INT(0, child_mode_flags(&cfg, NULL, out));
    TEST_ASSERT_NULL(child_mode_mode_flag(&cfg));
}

void test_yolo_mode_passes_yolo(void) {
    ApprovalGateConfig cfg = {.enabled = 0, .auto_mode = 1};
    const char *out[CHILD_MODE_FLAGS_MAX];
    TEST_ASSERT_EQUAL_INT(1, child_mode_flags(&cfg, NULL, out));
    TEST_ASSERT_EQUAL_STRING("--yolo", out[0]);
}

void test_auto_mode_passes_auto(void) {
    ApprovalGateConfig cfg = {.enabled = 1, .auto_mode = 1};
    const char *out[CHILD_MODE_FLAGS_MAX];
    TEST_ASSERT_EQUAL_INT(1, child_mode_flags(&cfg, "", out));
    TEST_ASSERT_EQUAL_STRING("--auto", out[0]);
}

void test_home_is_appended(void) {
    ApprovalGateConfig cfg = {.enabled = 1, .auto_mode = 1};
    const char *out[CHILD_MODE_FLAGS_MAX];
    TEST_ASSERT_EQUAL_INT(3, child_mode_flags(&cfg, "/tmp/h", out));
    TEST_ASSERT_EQUAL_STRING("--home", out[1]);
    TEST_ASSERT_EQUAL_STRING("/tmp/h", out[2]);
}

void test_null_config_is_normal_mode(void) {
    const char *out[CHILD_MODE_FLAGS_MAX];
    TEST_ASSERT_EQUAL_INT(0, child_mode_flags(NULL, NULL, out));
}

void test_relative_home_is_resolved_to_an_absolute_path(void) {
    /* A child runs in another directory, so a relative home would name a
     * different directory there. */
    char cwd[PATH_MAX];
    TEST_ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));
    mkdir("/tmp/child_mode_home", 0700);
    TEST_ASSERT_EQUAL_INT(0, chdir("/tmp"));

    char buf[PATH_MAX];
    TEST_ASSERT_EQUAL_INT(0, child_mode_absolute_home("child_mode_home", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("/tmp/child_mode_home", buf);

    /* A home that doesn't exist yet still becomes absolute. */
    TEST_ASSERT_EQUAL_INT(0, child_mode_absolute_home("missing_home/sub", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("/tmp/missing_home/sub", buf);

    TEST_ASSERT_EQUAL_INT(0, chdir(cwd));
}

void test_absolute_home_is_passed_through(void) {
    char buf[PATH_MAX];
    TEST_ASSERT_EQUAL_INT(0, child_mode_absolute_home("/tmp", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("/tmp", buf);

    TEST_ASSERT_EQUAL_INT(-1, child_mode_absolute_home(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, child_mode_absolute_home("", buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, child_mode_absolute_home("/tmp", NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, child_mode_absolute_home("/tmp", buf, 2));
}

void test_process_gate_registration(void) {
    ApprovalGateConfig cfg = {.enabled = 1};
    child_mode_set_process_gate(&cfg);
    TEST_ASSERT_TRUE(child_mode_get_process_gate() == &cfg);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_mode_has_no_flag);
    RUN_TEST(test_yolo_mode_passes_yolo);
    RUN_TEST(test_auto_mode_passes_auto);
    RUN_TEST(test_home_is_appended);
    RUN_TEST(test_null_config_is_normal_mode);
    RUN_TEST(test_relative_home_is_resolved_to_an_absolute_path);
    RUN_TEST(test_absolute_home_is_passed_through);
    RUN_TEST(test_process_gate_registration);
    return UNITY_END();
}
