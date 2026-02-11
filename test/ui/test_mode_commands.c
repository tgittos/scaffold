#include "../../test/unity/unity.h"
#include "ui/mode_commands.h"
#include "agent/session.h"
#include "agent/prompt_mode.h"
#include "ui/status_line.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AgentSession g_session;

void setUp(void) {
    memset(&g_session, 0, sizeof(g_session));
    g_session.current_mode = PROMPT_MODE_DEFAULT;
    status_line_init();
}

void tearDown(void) {
    status_line_cleanup();
}

void test_mode_command_null_params(void) {
    TEST_ASSERT_EQUAL_INT(-1, process_mode_command(NULL, &g_session));
    TEST_ASSERT_EQUAL_INT(-1, process_mode_command("", NULL));
}

void test_mode_command_show_current(void) {
    int result = process_mode_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_mode_command_list(void) {
    int result = process_mode_command("list", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_mode_command_switch_to_plan(void) {
    int result = process_mode_command("plan", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_PLAN, g_session.current_mode);
}

void test_mode_command_switch_to_debug(void) {
    int result = process_mode_command("debug", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEBUG, g_session.current_mode);
}

void test_mode_command_switch_to_explore(void) {
    int result = process_mode_command("explore", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_EXPLORE, g_session.current_mode);
}

void test_mode_command_switch_to_review(void) {
    int result = process_mode_command("review", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_REVIEW, g_session.current_mode);
}

void test_mode_command_switch_to_default(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    int result = process_mode_command("default", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEFAULT, g_session.current_mode);
}

void test_mode_command_switch_invalid(void) {
    int result = process_mode_command("nonexistent", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEFAULT, g_session.current_mode);
}

void test_mode_command_switch_back_and_forth(void) {
    process_mode_command("plan", &g_session);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_PLAN, g_session.current_mode);

    process_mode_command("debug", &g_session);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEBUG, g_session.current_mode);

    process_mode_command("default", &g_session);
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEFAULT, g_session.current_mode);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_mode_command_null_params);
    RUN_TEST(test_mode_command_show_current);
    RUN_TEST(test_mode_command_list);
    RUN_TEST(test_mode_command_switch_to_plan);
    RUN_TEST(test_mode_command_switch_to_debug);
    RUN_TEST(test_mode_command_switch_to_explore);
    RUN_TEST(test_mode_command_switch_to_review);
    RUN_TEST(test_mode_command_switch_to_default);
    RUN_TEST(test_mode_command_switch_invalid);
    RUN_TEST(test_mode_command_switch_back_and_forth);

    return UNITY_END();
}
