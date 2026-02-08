#include "unity.h"
#include "agent/message_dispatcher.h"
#include "llm/llm_provider.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {
    provider_registry_cleanup();
}

void test_select_mode_null_session(void) {
    DispatchDecision d = message_dispatcher_select_mode(NULL);
    TEST_ASSERT_EQUAL(DISPATCH_BUFFERED, d.mode);
    TEST_ASSERT_NULL(d.provider);
}

void test_select_mode_streaming_disabled(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    session.session_data.config.enable_streaming = 0;
    session.session_data.config.api_url = "https://api.openai.com/v1/chat/completions";

    DispatchDecision d = message_dispatcher_select_mode(&session);
    TEST_ASSERT_EQUAL(DISPATCH_BUFFERED, d.mode);
    TEST_ASSERT_NULL(d.provider);
}

void test_select_mode_streaming_enabled_openai(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    session.session_data.config.enable_streaming = 1;
    session.session_data.config.api_url = "https://api.openai.com/v1/chat/completions";

    DispatchDecision d = message_dispatcher_select_mode(&session);
    TEST_ASSERT_EQUAL(DISPATCH_STREAMING, d.mode);
    TEST_ASSERT_NOT_NULL(d.provider);
}

void test_select_mode_streaming_enabled_anthropic(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    session.session_data.config.enable_streaming = 1;
    session.session_data.config.api_url = "https://api.anthropic.com/v1/messages";

    DispatchDecision d = message_dispatcher_select_mode(&session);
    TEST_ASSERT_EQUAL(DISPATCH_STREAMING, d.mode);
    TEST_ASSERT_NOT_NULL(d.provider);
}

void test_select_mode_streaming_enabled_local_ai(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    session.session_data.config.enable_streaming = 1;
    session.session_data.config.api_url = "http://localhost:1234/v1/chat/completions";

    DispatchDecision d = message_dispatcher_select_mode(&session);
    TEST_ASSERT_EQUAL(DISPATCH_STREAMING, d.mode);
    TEST_ASSERT_NOT_NULL(d.provider);
}

void test_select_mode_streaming_enabled_null_url(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    session.session_data.config.enable_streaming = 1;
    session.session_data.config.api_url = NULL;

    DispatchDecision d = message_dispatcher_select_mode(&session);
    TEST_ASSERT_EQUAL(DISPATCH_BUFFERED, d.mode);
    TEST_ASSERT_NULL(d.provider);
}

void test_build_payload_null_session(void) {
    char* result = message_dispatcher_build_payload(NULL, "hello", 100);
    TEST_ASSERT_NULL(result);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_select_mode_null_session);
    RUN_TEST(test_select_mode_streaming_disabled);
    RUN_TEST(test_select_mode_streaming_enabled_openai);
    RUN_TEST(test_select_mode_streaming_enabled_anthropic);
    RUN_TEST(test_select_mode_streaming_enabled_local_ai);
    RUN_TEST(test_select_mode_streaming_enabled_null_url);
    RUN_TEST(test_build_payload_null_session);

    return UNITY_END();
}
