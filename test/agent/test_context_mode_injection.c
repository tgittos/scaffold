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
    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);
    /* Mode text goes into dynamic_context; base_prompt is just "Base prompt." */
    if (parts.dynamic_context != NULL) {
        TEST_ASSERT_NULL(strstr(parts.dynamic_context, "Active Mode Instructions"));
    }
    free_enhanced_prompt_parts(&parts);
}

void test_plan_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(parts.dynamic_context);
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "Active Mode Instructions"));
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "PLAN mode"));
    free_enhanced_prompt_parts(&parts);
}

void test_debug_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_DEBUG;
    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(parts.dynamic_context);
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "DEBUG mode"));
    free_enhanced_prompt_parts(&parts);
}

void test_explore_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_EXPLORE;
    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(parts.dynamic_context);
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "EXPLORE mode"));
    free_enhanced_prompt_parts(&parts);
}

void test_review_mode_injects_text(void) {
    g_session.current_mode = PROMPT_MODE_REVIEW;
    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(parts.dynamic_context);
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "REVIEW mode"));
    free_enhanced_prompt_parts(&parts);
}

void test_mode_text_in_dynamic_context(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Base prompt should be the session system prompt */
    TEST_ASSERT_NOT_NULL(strstr(parts.base_prompt, "Base prompt."));
    /* Mode text should be in dynamic context, not base */
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "PLAN mode"));
    free_enhanced_prompt_parts(&parts);
}

void test_switching_modes_changes_prompt(void) {
    g_session.current_mode = PROMPT_MODE_PLAN;
    EnhancedPromptParts parts1;
    build_enhanced_prompt_parts(&g_session, NULL, &parts1);
    TEST_ASSERT_NOT_NULL(strstr(parts1.dynamic_context, "PLAN mode"));

    g_session.current_mode = PROMPT_MODE_DEBUG;
    EnhancedPromptParts parts2;
    build_enhanced_prompt_parts(&g_session, NULL, &parts2);
    TEST_ASSERT_NOT_NULL(strstr(parts2.dynamic_context, "DEBUG mode"));
    TEST_ASSERT_NULL(strstr(parts2.dynamic_context, "PLAN mode"));

    free_enhanced_prompt_parts(&parts1);
    free_enhanced_prompt_parts(&parts2);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_default_mode_no_mode_section);
    RUN_TEST(test_plan_mode_injects_text);
    RUN_TEST(test_debug_mode_injects_text);
    RUN_TEST(test_explore_mode_injects_text);
    RUN_TEST(test_review_mode_injects_text);
    RUN_TEST(test_mode_text_in_dynamic_context);
    RUN_TEST(test_switching_modes_changes_prompt);

    return UNITY_END();
}
