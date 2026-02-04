#include "network/api_common.h"
#include "session/conversation_tracker.h"
#include "unity.h"
#include "db/document_store.h"
#include <stdlib.h>
#include "util/ralph_home.h"

// Test that reproduces the "messages array misformatted" bug
// The bug occurs when conversation history contains messages with NULL role or content

void setUp(void) {
    ralph_home_init(NULL);
    // Clear conversation data to ensure test isolation
    document_store_clear_conversations();
}

void tearDown(void) {
    // Unity teardown

    ralph_home_cleanup();
}

// Test format_openai_message with NULL role
void test_format_openai_message_null_role(void) {
    ConversationMessage message = {
        .role = NULL,  // This will cause strcmp to crash/behave unpredictably
        .content = "test content",
        .tool_call_id = NULL,
        .tool_name = NULL
    };
    
    char buffer[1024];
    int result = format_openai_message(buffer, sizeof(buffer), &message, 1);
    
    // The function should fail gracefully with NULL role
    TEST_ASSERT_EQUAL(-1, result);
}

// Test format_openai_message with NULL content  
void test_format_openai_message_null_content(void) {
    ConversationMessage message = {
        .role = "user",
        .content = NULL,  // This will cause json_build_message to fail
        .tool_call_id = NULL,
        .tool_name = NULL
    };
    
    char buffer[1024];
    int result = format_openai_message(buffer, sizeof(buffer), &message, 1);
    
    // The function should fail gracefully with NULL content
    TEST_ASSERT_EQUAL(-1, result);
}

// Test that conversation history with corrupted messages causes issues
void test_conversation_with_corrupted_messages(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add a normal message first
    TEST_ASSERT_EQUAL(0, append_conversation_message(&history, "user", "test message"));
    
    // Now manually corrupt the history by setting a message role to NULL
    if (history.count > 0) {
        free(history.data[0].role);
        history.data[0].role = NULL;  // This creates the undefined/null role condition
    }
    
    // Try to build JSON using the actual API function
    char buffer[4096];
    int result = build_messages_json(buffer, sizeof(buffer), "system prompt", &history, 
                                   "user message", format_openai_message, 0);
    
    // This should fail gracefully
    TEST_ASSERT_EQUAL(-1, result);
    
    cleanup_conversation_history(&history);
}

int main(void) {
    UNITY_BEGIN();
    
    // Now that the bug is fixed, we can safely test NULL role
    RUN_TEST(test_format_openai_message_null_role);
    RUN_TEST(test_format_openai_message_null_content);
    RUN_TEST(test_conversation_with_corrupted_messages);
    
    return UNITY_END();
}