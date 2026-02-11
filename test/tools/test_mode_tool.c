#include "../../test/unity/unity.h"
#include "tools/mode_tool.h"
#include "agent/session.h"
#include "agent/prompt_mode.h"
#include "ui/status_line.h"
#include <stdlib.h>
#include <string.h>

static AgentSession g_session;

void setUp(void) {
    memset(&g_session, 0, sizeof(g_session));
    g_session.current_mode = PROMPT_MODE_DEFAULT;
    status_line_init();
    mode_tool_set_session(&g_session);
}

void tearDown(void) {
    status_line_cleanup();
    mode_tool_set_session(NULL);
}

static ToolResult execute_switch(const char* mode_arg) {
    char args[128];
    snprintf(args, sizeof(args), "{\"mode\": \"%s\"}", mode_arg);

    ToolCall call = {
        .id = "test-call-1",
        .name = "switch_mode",
        .arguments = args,
    };

    ToolResult result = {0};
    execute_switch_mode_tool_call(&call, &result);
    return result;
}

static void free_result(ToolResult* r) {
    free(r->result);
    free(r->tool_call_id);
}

void test_switch_to_plan(void) {
    ToolResult r = execute_switch("plan");
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_PLAN, g_session.current_mode);
    TEST_ASSERT_TRUE(r.success);
    TEST_ASSERT_NOT_NULL(strstr(r.result, "plan"));
    free_result(&r);
}

void test_switch_to_debug(void) {
    ToolResult r = execute_switch("debug");
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEBUG, g_session.current_mode);
    TEST_ASSERT_TRUE(r.success);
    free_result(&r);
}

void test_switch_to_explore(void) {
    ToolResult r = execute_switch("explore");
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_EXPLORE, g_session.current_mode);
    TEST_ASSERT_TRUE(r.success);
    free_result(&r);
}

void test_switch_to_review(void) {
    ToolResult r = execute_switch("review");
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_REVIEW, g_session.current_mode);
    TEST_ASSERT_TRUE(r.success);
    free_result(&r);
}

void test_switch_to_default(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    ToolResult r = execute_switch("default");
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEFAULT, g_session.current_mode);
    TEST_ASSERT_TRUE(r.success);
    free_result(&r);
}

void test_switch_invalid_mode(void) {
    ToolResult r = execute_switch("nonexistent");
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEFAULT, g_session.current_mode);
    TEST_ASSERT_FALSE(r.success);
    TEST_ASSERT_NOT_NULL(strstr(r.result, "Unknown mode"));
    free_result(&r);
}

void test_switch_no_session(void) {
    mode_tool_set_session(NULL);
    ToolResult r = execute_switch("plan");
    TEST_ASSERT_FALSE(r.success);
    TEST_ASSERT_NOT_NULL(strstr(r.result, "not initialized"));
    free_result(&r);
}

void test_switch_reports_old_and_new(void) {
    g_session.current_mode = PROMPT_MODE_EXPLORE;
    ToolResult r = execute_switch("debug");
    TEST_ASSERT_NOT_NULL(strstr(r.result, "explore"));
    TEST_ASSERT_NOT_NULL(strstr(r.result, "debug"));
    free_result(&r);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_switch_to_plan);
    RUN_TEST(test_switch_to_debug);
    RUN_TEST(test_switch_to_explore);
    RUN_TEST(test_switch_to_review);
    RUN_TEST(test_switch_to_default);
    RUN_TEST(test_switch_invalid_mode);
    RUN_TEST(test_switch_no_session);
    RUN_TEST(test_switch_reports_old_and_new);

    return UNITY_END();
}
