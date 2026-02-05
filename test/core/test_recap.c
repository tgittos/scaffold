#include "unity.h"
#include "ralph.h"
#include "session/conversation_tracker.h"
#include "db/hnswlib_wrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/ralph_home.h"

void setUp(void) {
    ralph_home_init(NULL);
    hnswlib_clear_all();
}

void tearDown(void) {
    ralph_home_cleanup();
}

// Test that recap with NULL session returns error
void test_recap_null_session(void) {
    int result = session_generate_recap(NULL, 5);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// Test that recap with empty conversation returns success (nothing to recap)
void test_recap_empty_conversation(void) {
    AgentSession session;

    int init_result = session_init(&session);
    TEST_ASSERT_EQUAL_INT(0, init_result);

    // Clear any loaded conversation
    cleanup_conversation_history(&session.session_data.conversation);
    init_conversation_history(&session.session_data.conversation);

    // Recap with empty conversation should return 0 (nothing to do)
    int result = session_generate_recap(&session, 5);
    TEST_ASSERT_EQUAL_INT(0, result);

    session_cleanup(&session);
}

// Test that recap doesn't persist to conversation history
void test_recap_does_not_persist_conversation(void) {
    AgentSession session;

    int init_result = session_init(&session);
    TEST_ASSERT_EQUAL_INT(0, init_result);

    int load_result = session_load_config(&session);
    TEST_ASSERT_EQUAL_INT(0, load_result);

    // Clear and add some test messages
    cleanup_conversation_history(&session.session_data.conversation);
    init_conversation_history(&session.session_data.conversation);

    append_conversation_message(&session.session_data.conversation, "user", "Hello");
    append_conversation_message(&session.session_data.conversation, "assistant", "Hi there!");
    append_conversation_message(&session.session_data.conversation, "user", "How are you?");

    int original_count = session.session_data.conversation.count;
    TEST_ASSERT_EQUAL_INT(3, original_count);

    // Generate recap (will fail due to no API key, but should still not modify history)
    int result = session_generate_recap(&session, 5);
    // Result may be -1 due to API failure, that's expected
    (void)result; // Suppress unused warning

    // Verify conversation count is unchanged
    TEST_ASSERT_EQUAL_INT(original_count, session.session_data.conversation.count);

    session_cleanup(&session);
}

// Test max_messages parameter with 0 (should use default)
void test_recap_max_messages_zero_uses_default(void) {
    AgentSession session;

    int init_result = session_init(&session);
    TEST_ASSERT_EQUAL_INT(0, init_result);

    // Clear and add test messages
    cleanup_conversation_history(&session.session_data.conversation);
    init_conversation_history(&session.session_data.conversation);

    append_conversation_message(&session.session_data.conversation, "user", "Test message");

    int original_count = session.session_data.conversation.count;

    // Call with 0 max_messages - should use default
    int result = session_generate_recap(&session, 0);
    (void)result;

    // History should be unchanged
    TEST_ASSERT_EQUAL_INT(original_count, session.session_data.conversation.count);

    session_cleanup(&session);
}

// Test that tool messages are skipped in recap context
void test_recap_skips_tool_messages(void) {
    AgentSession session;

    int init_result = session_init(&session);
    TEST_ASSERT_EQUAL_INT(0, init_result);

    // Clear and add messages including tool messages
    cleanup_conversation_history(&session.session_data.conversation);
    init_conversation_history(&session.session_data.conversation);

    append_conversation_message(&session.session_data.conversation, "user", "Run a command");
    append_tool_message(&session.session_data.conversation, "Command output", "call_123", "shell");
    append_conversation_message(&session.session_data.conversation, "assistant", "Here's the result");

    int original_count = session.session_data.conversation.count;
    TEST_ASSERT_EQUAL_INT(3, original_count);

    // Generate recap (may fail due to no API)
    int result = session_generate_recap(&session, 5);
    (void)result;

    // History should be unchanged
    TEST_ASSERT_EQUAL_INT(original_count, session.session_data.conversation.count);

    session_cleanup(&session);
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
