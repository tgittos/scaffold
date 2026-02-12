#include "unity.h"
#include "agent/session.h"
#include "session/session_manager.h"
#include "session/conversation_tracker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AgentSession g_session;

static void init_bare_session(AgentSession* s) {
    memset(s, 0, sizeof(*s));
    session_data_init(&s->session_data);
}

static void cleanup_bare_session(AgentSession* s) {
    session_data_cleanup(&s->session_data);
}

void setUp(void) {}
void tearDown(void) {}

void test_recap_null_session(void) {
    int result = session_generate_recap(NULL, 5);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_recap_empty_conversation(void) {
    init_bare_session(&g_session);

    int result = session_generate_recap(&g_session, 5);
    TEST_ASSERT_EQUAL_INT(0, result);

    cleanup_bare_session(&g_session);
}

void test_recap_does_not_persist_conversation(void) {
    init_bare_session(&g_session);

    append_conversation_message(&g_session.session_data.conversation, "user", "Hello");
    append_conversation_message(&g_session.session_data.conversation, "assistant", "Hi there!");
    append_conversation_message(&g_session.session_data.conversation, "user", "How are you?");

    int original_count = g_session.session_data.conversation.count;
    TEST_ASSERT_EQUAL_INT(3, original_count);

    int result = session_generate_recap(&g_session, 5);
    (void)result;

    TEST_ASSERT_EQUAL_INT(original_count, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

void test_recap_max_messages_zero_uses_default(void) {
    init_bare_session(&g_session);

    append_conversation_message(&g_session.session_data.conversation, "user", "Test message");

    int original_count = g_session.session_data.conversation.count;

    int result = session_generate_recap(&g_session, 0);
    (void)result;

    TEST_ASSERT_EQUAL_INT(original_count, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

void test_recap_skips_tool_messages(void) {
    init_bare_session(&g_session);

    append_conversation_message(&g_session.session_data.conversation, "user", "Run a command");
    append_tool_message(&g_session.session_data.conversation, "Command output", "call_123", "shell");
    append_conversation_message(&g_session.session_data.conversation, "assistant", "Here's the result");

    int original_count = g_session.session_data.conversation.count;
    TEST_ASSERT_EQUAL_INT(3, original_count);

    int result = session_generate_recap(&g_session, 5);
    (void)result;

    TEST_ASSERT_EQUAL_INT(original_count, g_session.session_data.conversation.count);

    cleanup_bare_session(&g_session);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_recap_null_session);
    RUN_TEST(test_recap_empty_conversation);
    RUN_TEST(test_recap_does_not_persist_conversation);
    RUN_TEST(test_recap_max_messages_zero_uses_default);
    RUN_TEST(test_recap_skips_tool_messages);

    return UNITY_END();
}
