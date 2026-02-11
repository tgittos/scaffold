#include "../../test/unity/unity.h"
#include "ui/model_commands.h"
#include "agent/session.h"
#include "util/config.h"
#include "util/ralph_home.h"
#include "llm/model_capabilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static AgentSession g_session;

void setUp(void) {
    config_cleanup();
    ralph_home_init(NULL);
    config_init();

    memset(&g_session, 0, sizeof(g_session));
    g_session.session_data.config.model = strdup("gpt-5-mini-2025-08-07");
    g_session.session_data.config.api_url = strdup("https://api.openai.com/v1/chat/completions");
    g_session.model_registry = get_model_registry();
}

void tearDown(void) {
    free(g_session.session_data.config.model);
    free(g_session.session_data.config.api_url);
    config_cleanup();
    ralph_home_cleanup();
}

void test_model_command_null_params(void) {
    TEST_ASSERT_EQUAL_INT(-1, process_model_command(NULL, &g_session));
    TEST_ASSERT_EQUAL_INT(-1, process_model_command("", NULL));
}

void test_model_command_show_current(void) {
    int result = process_model_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_model_command_list(void) {
    int result = process_model_command("list", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_model_command_switch_tier(void) {
    int result = process_model_command("simple", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("o4-mini", g_session.session_data.config.model);
}

void test_model_command_switch_raw_id(void) {
    int result = process_model_command("gpt-4o-2025-06-01", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("gpt-4o-2025-06-01", g_session.session_data.config.model);
}

void test_model_command_switch_high(void) {
    int result = process_model_command("high", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("gpt-5.2-2025-12-11", g_session.session_data.config.model);
}

void test_model_command_switch_updates_config(void) {
    process_model_command("simple", &g_session);
    TEST_ASSERT_EQUAL_STRING("o4-mini", config_get_string("model"));
}

void test_model_command_switch_back_to_standard(void) {
    process_model_command("simple", &g_session);
    TEST_ASSERT_EQUAL_STRING("o4-mini", g_session.session_data.config.model);

    process_model_command("standard", &g_session);
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", g_session.session_data.config.model);
}

void test_model_command_claude_on_openai_rejected(void) {
    process_model_command("claude-3-opus", &g_session);
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", g_session.session_data.config.model);
}

void test_model_command_non_claude_on_anthropic_rejected(void) {
    free(g_session.session_data.config.api_url);
    g_session.session_data.config.api_url = strdup("https://api.anthropic.com/v1/messages");

    process_model_command("gpt-5-mini-2025-08-07", &g_session);
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", g_session.session_data.config.model);

    process_model_command("o4-mini", &g_session);
    TEST_ASSERT_EQUAL_STRING("gpt-5-mini-2025-08-07", g_session.session_data.config.model);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_model_command_null_params);
    RUN_TEST(test_model_command_show_current);
    RUN_TEST(test_model_command_list);
    RUN_TEST(test_model_command_switch_tier);
    RUN_TEST(test_model_command_switch_raw_id);
    RUN_TEST(test_model_command_switch_high);
    RUN_TEST(test_model_command_switch_updates_config);
    RUN_TEST(test_model_command_switch_back_to_standard);
    RUN_TEST(test_model_command_claude_on_openai_rejected);
    RUN_TEST(test_model_command_non_claude_on_anthropic_rejected);

    return UNITY_END();
}
