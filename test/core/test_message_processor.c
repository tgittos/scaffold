#include "unity.h"
#include "agent/message_processor.h"
#include "session/conversation_tracker.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

void test_handle_response_null_session(void) {
    LLMRoundTripResult rt;
    memset(&rt, 0, sizeof(rt));
    TEST_ASSERT_EQUAL(-1, message_processor_handle_response(NULL, &rt, "hello", 100));
}

void test_handle_response_null_result(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    TEST_ASSERT_EQUAL(-1, message_processor_handle_response(&session, NULL, "hello", 100));
}

void test_handle_response_takes_tool_call_ownership(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    LLMRoundTripResult rt;
    memset(&rt, 0, sizeof(rt));

    /* Set up a fake tool call */
    rt.tool_calls = calloc(1, sizeof(ToolCall));
    rt.tool_calls[0].name = strdup("test_tool");
    rt.tool_calls[0].id = strdup("call_123");
    rt.tool_calls[0].arguments = strdup("{}");
    rt.tool_call_count = 1;

    rt.parsed.response_content = strdup("I'll use a tool");

    /* The function takes ownership of tool_calls */
    message_processor_handle_response(&session, &rt, "hello", 100);

    TEST_ASSERT_NULL(rt.tool_calls);
    TEST_ASSERT_EQUAL(0, rt.tool_call_count);

    /* ParsedResponse fields still owned by caller */
    TEST_ASSERT_NOT_NULL(rt.parsed.response_content);

    cleanup_parsed_response(&rt.parsed);
    cleanup_conversation_history(&session.session_data.conversation);
}

void test_handle_response_no_tools_appends_conversation(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    LLMRoundTripResult rt;
    memset(&rt, 0, sizeof(rt));
    rt.parsed.response_content = strdup("Hello! How can I help?");
    rt.tool_calls = NULL;
    rt.tool_call_count = 0;

    int rc = message_processor_handle_response(&session, &rt, "hi there", 100);
    TEST_ASSERT_EQUAL(0, rc);

    /* Should have appended user + assistant messages */
    TEST_ASSERT_EQUAL(2, session.session_data.conversation.count);
    TEST_ASSERT_EQUAL_STRING("user", session.session_data.conversation.data[0].role);
    TEST_ASSERT_EQUAL_STRING("hi there", session.session_data.conversation.data[0].content);
    TEST_ASSERT_EQUAL_STRING("assistant", session.session_data.conversation.data[1].role);
    TEST_ASSERT_EQUAL_STRING("Hello! How can I help?", session.session_data.conversation.data[1].content);

    cleanup_parsed_response(&rt.parsed);
    cleanup_conversation_history(&session.session_data.conversation);
}

void test_handle_response_no_tools_thinking_fallback(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    LLMRoundTripResult rt;
    memset(&rt, 0, sizeof(rt));
    rt.parsed.response_content = NULL;
    rt.parsed.thinking_content = strdup("Let me think about that...");
    rt.tool_calls = NULL;
    rt.tool_call_count = 0;

    int rc = message_processor_handle_response(&session, &rt, "question", 100);
    TEST_ASSERT_EQUAL(0, rc);

    TEST_ASSERT_EQUAL(2, session.session_data.conversation.count);
    TEST_ASSERT_EQUAL_STRING("assistant", session.session_data.conversation.data[1].role);
    TEST_ASSERT_EQUAL_STRING("Let me think about that...", session.session_data.conversation.data[1].content);

    cleanup_parsed_response(&rt.parsed);
    cleanup_conversation_history(&session.session_data.conversation);
}

void test_handle_response_no_content_no_tools(void) {
    AgentSession session;
    memset(&session, 0, sizeof(session));
    init_conversation_history(&session.session_data.conversation);

    LLMRoundTripResult rt;
    memset(&rt, 0, sizeof(rt));
    rt.parsed.response_content = NULL;
    rt.parsed.thinking_content = NULL;
    rt.tool_calls = NULL;
    rt.tool_call_count = 0;

    int rc = message_processor_handle_response(&session, &rt, "empty", 100);
    TEST_ASSERT_EQUAL(0, rc);

    /* Only user message appended; assistant had nothing to save */
    TEST_ASSERT_EQUAL(1, session.session_data.conversation.count);
    TEST_ASSERT_EQUAL_STRING("user", session.session_data.conversation.data[0].role);

    cleanup_conversation_history(&session.session_data.conversation);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_handle_response_null_session);
    RUN_TEST(test_handle_response_null_result);
    RUN_TEST(test_handle_response_takes_tool_call_ownership);
    RUN_TEST(test_handle_response_no_tools_appends_conversation);
    RUN_TEST(test_handle_response_no_tools_thinking_fallback);
    RUN_TEST(test_handle_response_no_content_no_tools);

    return UNITY_END();
}
