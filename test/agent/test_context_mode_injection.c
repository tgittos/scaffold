#include "../../test/unity/unity.h"
#include "agent/session.h"
#include "agent/context_enhancement.h"
#include "agent/prompt_mode.h"
#include <stdlib.h>
#include <string.h>

static AgentSession g_session;

void setUp(void) {
    memset(&g_session, 0, sizeof(g_session));
    g_session.session_data.config.system_prompt = strdup("Base prompt.");
    g_session.current_mode = PROMPT_MODE_DEFAULT;
    todo_list_init(&g_session.todo_list);
}

void tearDown(void) {
    free(g_session.session_data.config.system_prompt);
    todo_list_destroy(&g_session.todo_list);
}

void test_default_mode_no_mode_section(void) {
    g_session.current_mode = PROMPT_MODE_DEFAULT;
    char* prompt = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_NULL(strstr(prompt, "Active Mode Instructions"));
    free(prompt);
}

void test_plan_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    char* prompt = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_NOT_NULL(strstr(prompt, "Active Mode Instructions"));
    TEST_ASSERT_NOT_NULL(strstr(prompt, "PLAN mode"));
    free(prompt);
}

void test_debug_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_DEBUG;
    char* prompt = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_NOT_NULL(strstr(prompt, "DEBUG mode"));
    free(prompt);
}

void test_explore_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_EXPLORE;
    char* prompt = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_NOT_NULL(strstr(prompt, "EXPLORE mode"));
    free(prompt);
}

void test_review_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_REVIEW;
    char* prompt = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(prompt);
    TEST_ASSERT_NOT_NULL(strstr(prompt, "REVIEW mode"));
    free(prompt);
}

void test_mode_text_after_base_prompt(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    char* prompt = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(prompt);

    char* base = strstr(prompt, "Base prompt.");
    char* mode = strstr(prompt, "PLAN mode");
    TEST_ASSERT_NOT_NULL(base);
    TEST_ASSERT_NOT_NULL(mode);
    TEST_ASSERT_TRUE(mode > base);
    free(prompt);
}

void test_switching_modes_changes_prompt(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    char* prompt1 = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(strstr(prompt1, "PLAN mode"));
    TEST_ASSERT_NULL(strstr(prompt1, "DEBUG mode"));

    g_session.current_mode = PROMPT_MODE_DEBUG;
    char* prompt2 = build_enhanced_prompt_with_context(&g_session, NULL);
    TEST_ASSERT_NOT_NULL(strstr(prompt2, "DEBUG mode"));
    TEST_ASSERT_NULL(strstr(prompt2, "PLAN mode"));

    free(prompt1);
    free(prompt2);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_default_mode_no_mode_section);
    RUN_TEST(test_plan_mode_injects_text);
    RUN_TEST(test_debug_mode_injects_text);
    RUN_TEST(test_explore_mode_injects_text);
    RUN_TEST(test_review_mode_injects_text);
    RUN_TEST(test_mode_text_after_base_prompt);
    RUN_TEST(test_switching_modes_changes_prompt);

    return UNITY_END();
}
