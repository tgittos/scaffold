#include "../../test/unity/unity.h"
#include "agent/prompt_mode.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_name_returns_correct_strings(void) {
    TEST_ASSERT_EQUAL_STRING("default", prompt_mode_name(PROMPT_MODE_DEFAULT));
    TEST_ASSERT_EQUAL_STRING("plan", prompt_mode_name(PROMPT_MODE_PLAN));
    TEST_ASSERT_EQUAL_STRING("explore", prompt_mode_name(PROMPT_MODE_EXPLORE));
    TEST_ASSERT_EQUAL_STRING("debug", prompt_mode_name(PROMPT_MODE_DEBUG));
    TEST_ASSERT_EQUAL_STRING("review", prompt_mode_name(PROMPT_MODE_REVIEW));
}

void test_name_out_of_range(void) {
    TEST_ASSERT_EQUAL_STRING("default", prompt_mode_name((PromptMode)-1));
    TEST_ASSERT_EQUAL_STRING("default", prompt_mode_name(PROMPT_MODE_COUNT));
    TEST_ASSERT_EQUAL_STRING("default", prompt_mode_name((PromptMode)99));
}

void test_from_name_valid(void) {
    PromptMode mode;
    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("default", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEFAULT, mode);

    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("plan", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_PLAN, mode);

    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("explore", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_EXPLORE, mode);

    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("debug", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEBUG, mode);

    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("review", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_REVIEW, mode);
}

void test_from_name_case_insensitive(void) {
    PromptMode mode;
    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("PLAN", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_PLAN, mode);

    TEST_ASSERT_EQUAL_INT(0, prompt_mode_from_name("Debug", &mode));
    TEST_ASSERT_EQUAL_INT(PROMPT_MODE_DEBUG, mode);
}

void test_from_name_invalid(void) {
    PromptMode mode = PROMPT_MODE_DEFAULT;
    TEST_ASSERT_EQUAL_INT(-1, prompt_mode_from_name("nonexistent", &mode));
    TEST_ASSERT_EQUAL_INT(-1, prompt_mode_from_name("", &mode));
    TEST_ASSERT_EQUAL_INT(-1, prompt_mode_from_name(NULL, &mode));
    TEST_ASSERT_EQUAL_INT(-1, prompt_mode_from_name("plan", NULL));
}

void test_get_text_default_returns_null(void) {
    TEST_ASSERT_NULL(prompt_mode_get_text(PROMPT_MODE_DEFAULT));
}

void test_get_text_non_default_returns_content(void) {
    const char* plan_text = prompt_mode_get_text(PROMPT_MODE_PLAN);
    TEST_ASSERT_NOT_NULL(plan_text);
    TEST_ASSERT_TRUE(strlen(plan_text) > 0);
    TEST_ASSERT_NOT_NULL(strstr(plan_text, "PLAN mode"));

    const char* explore_text = prompt_mode_get_text(PROMPT_MODE_EXPLORE);
    TEST_ASSERT_NOT_NULL(explore_text);
    TEST_ASSERT_NOT_NULL(strstr(explore_text, "EXPLORE mode"));

    const char* debug_text = prompt_mode_get_text(PROMPT_MODE_DEBUG);
    TEST_ASSERT_NOT_NULL(debug_text);
    TEST_ASSERT_NOT_NULL(strstr(debug_text, "DEBUG mode"));

    const char* review_text = prompt_mode_get_text(PROMPT_MODE_REVIEW);
    TEST_ASSERT_NOT_NULL(review_text);
    TEST_ASSERT_NOT_NULL(strstr(review_text, "REVIEW mode"));
}

void test_get_text_out_of_range(void) {
    TEST_ASSERT_NULL(prompt_mode_get_text((PromptMode)-1));
    TEST_ASSERT_NULL(prompt_mode_get_text(PROMPT_MODE_COUNT));
}

void test_description_returns_non_empty(void) {
    for (int i = 0; i < PROMPT_MODE_COUNT; i++) {
        const char* desc = prompt_mode_description((PromptMode)i);
        TEST_ASSERT_NOT_NULL(desc);
        TEST_ASSERT_TRUE(strlen(desc) > 0);
    }
}

void test_description_out_of_range(void) {
    const char* desc = prompt_mode_description((PromptMode)-1);
    TEST_ASSERT_EQUAL_STRING("", desc);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_name_returns_correct_strings);
    RUN_TEST(test_name_out_of_range);
    RUN_TEST(test_from_name_valid);
    RUN_TEST(test_from_name_case_insensitive);
    RUN_TEST(test_from_name_invalid);
    RUN_TEST(test_get_text_default_returns_null);
    RUN_TEST(test_get_text_non_default_returns_content);
    RUN_TEST(test_get_text_out_of_range);
    RUN_TEST(test_description_returns_non_empty);
    RUN_TEST(test_description_out_of_range);

    return UNITY_END();
}
