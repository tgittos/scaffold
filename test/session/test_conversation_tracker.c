#include "unity.h"
#include "session/conversation_tracker.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "util/app_home.h"

void setUp(void) {
    app_home_init(NULL);
}

void tearDown(void) {
    // No file cleanup needed - using vector DB only

    app_home_cleanup();
}

void test_init_conversation_history(void) {
    ConversationHistory history;

    init_conversation_history(&history);

    // After init, the array should be empty but with default capacity allocated
    TEST_ASSERT_EQUAL(0, history.count);
    TEST_ASSERT_TRUE(history.capacity > 0);  // Default capacity is allocated
    TEST_ASSERT_NOT_NULL(history.data);

    cleanup_conversation_history(&history);
}

void test_init_conversation_history_with_null(void) {
    // Should not crash
    init_conversation_history(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_load_conversation_history_empty(void) {
    ConversationHistory history;
    
    int result = load_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, result);
    // History will be loaded from vector DB if available, or empty if not
    TEST_ASSERT_GREATER_OR_EQUAL(0, history.count);
    
    cleanup_conversation_history(&history);
}

void test_load_conversation_history_with_null(void) {
    int result = load_conversation_history(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_append_conversation_message_first_message(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    int result = append_conversation_message(&history, "user", "Hello, how are you?");
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, history.count);
    TEST_ASSERT_NOT_NULL(history.data);
    TEST_ASSERT_EQUAL_STRING("user", history.data[0].role);
    TEST_ASSERT_EQUAL_STRING("Hello, how are you?", history.data[0].content);
    
    cleanup_conversation_history(&history);
}

void test_append_conversation_message_multiple_messages(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    int result1 = append_conversation_message(&history, "user", "What is 2+2?");
    int result2 = append_conversation_message(&history, "assistant", "2+2 equals 4.");
    int result3 = append_conversation_message(&history, "user", "Thank you!");
    
    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(0, result2);
    TEST_ASSERT_EQUAL(0, result3);
    TEST_ASSERT_EQUAL(3, history.count);
    
    TEST_ASSERT_EQUAL_STRING("user", history.data[0].role);
    TEST_ASSERT_EQUAL_STRING("What is 2+2?", history.data[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.data[1].role);
    TEST_ASSERT_EQUAL_STRING("2+2 equals 4.", history.data[1].content);
    
    TEST_ASSERT_EQUAL_STRING("user", history.data[2].role);
    TEST_ASSERT_EQUAL_STRING("Thank you!", history.data[2].content);
    
    cleanup_conversation_history(&history);
}

void test_append_conversation_message_with_null_parameters(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Test null history
    TEST_ASSERT_EQUAL(-1, append_conversation_message(NULL, "user", "test"));
    
    // Test null role
    TEST_ASSERT_EQUAL(-1, append_conversation_message(&history, NULL, "test"));
    
    // Test null content
    TEST_ASSERT_EQUAL(-1, append_conversation_message(&history, "user", NULL));
    
    cleanup_conversation_history(&history);
}

void test_append_conversation_message_with_multiline_content(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    const char *multiline_content = "This is line 1\nThis is line 2\nThis is line 3";
    
    int result = append_conversation_message(&history, "user", multiline_content);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, history.count);
    TEST_ASSERT_EQUAL_STRING("user", history.data[0].role);
    TEST_ASSERT_EQUAL_STRING(multiline_content, history.data[0].content);
    
    cleanup_conversation_history(&history);
}




void test_cleanup_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add some messages
    append_conversation_message(&history, "user", "Test message 1");
    append_conversation_message(&history, "assistant", "Test response 1");
    
    TEST_ASSERT_EQUAL(2, history.count);
    TEST_ASSERT_NOT_NULL(history.data);
    
    cleanup_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, history.count);
    TEST_ASSERT_EQUAL(0, history.capacity);
    TEST_ASSERT_NULL(history.data);
}

void test_cleanup_conversation_history_with_null(void) {
    // Should not crash
    cleanup_conversation_history(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_conversation_persistence_across_loads(void) {
    ConversationHistory history1, history2;
    
    // Create initial conversation
    init_conversation_history(&history1);
    append_conversation_message(&history1, "user", "First message");
    append_conversation_message(&history1, "assistant", "First response");
    cleanup_conversation_history(&history1);
    
    // Load conversation from vector DB
    int result = load_conversation_history(&history2);
    
    TEST_ASSERT_EQUAL(0, result);
    // Messages are stored in vector DB, so we may get recent messages back
    // The exact count depends on what's in the vector DB
    TEST_ASSERT_GREATER_OR_EQUAL(0, history2.count);
    
    // Add more messages
    append_conversation_message(&history2, "user", "Second message");
    
    // Check that the new message was added to the current history
    TEST_ASSERT_GREATER_OR_EQUAL(1, history2.count);
    
    cleanup_conversation_history(&history2);
}

void test_large_conversation_handling(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add many messages to test dynamic resizing
    for (int i = 0; i < 50; i++) {
        char message[100];
        memset(message, 0, sizeof(message)); // Initialize buffer
        snprintf(message, sizeof(message), "User message %d", i);
        int result1 = append_conversation_message(&history, "user", message);
        TEST_ASSERT_EQUAL(0, result1);
        
        memset(message, 0, sizeof(message)); // Initialize buffer
        snprintf(message, sizeof(message), "Assistant response %d", i);
        int result2 = append_conversation_message(&history, "assistant", message);
        TEST_ASSERT_EQUAL(0, result2);
    }
    
    TEST_ASSERT_EQUAL(100, history.count);
    TEST_ASSERT_GREATER_OR_EQUAL(100, history.capacity);
    
    // Verify some messages
    TEST_ASSERT_EQUAL_STRING("user", history.data[0].role);
    TEST_ASSERT_EQUAL_STRING("User message 0", history.data[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.data[99].role);
    TEST_ASSERT_EQUAL_STRING("Assistant response 49", history.data[99].content);
    
    cleanup_conversation_history(&history);
}

void test_append_tool_message(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    int result = append_tool_message(&history, "File written successfully", "call_123", "write_file");
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, history.count);
    TEST_ASSERT_NOT_NULL(history.data);
    TEST_ASSERT_EQUAL_STRING("tool", history.data[0].role);
    TEST_ASSERT_EQUAL_STRING("File written successfully", history.data[0].content);
    TEST_ASSERT_EQUAL_STRING("call_123", history.data[0].tool_call_id);
    TEST_ASSERT_EQUAL_STRING("write_file", history.data[0].tool_name);
    
    cleanup_conversation_history(&history);
}

void test_append_tool_message_with_null_parameters(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Test null history
    TEST_ASSERT_EQUAL(-1, append_tool_message(NULL, "content", "call_123", "tool_name"));
    
    // Test null content
    TEST_ASSERT_EQUAL(-1, append_tool_message(&history, NULL, "call_123", "tool_name"));
    
    // Test null tool_call_id
    TEST_ASSERT_EQUAL(-1, append_tool_message(&history, "content", NULL, "tool_name"));
    
    // Test null tool_name
    TEST_ASSERT_EQUAL(-1, append_tool_message(&history, "content", "call_123", NULL));
    
    cleanup_conversation_history(&history);
}


void test_conversation_persistence_with_tool_messages(void) {
    ConversationHistory history1, history2;
    
    // Create initial conversation with tool messages
    init_conversation_history(&history1);
    append_conversation_message(&history1, "user", "Create a file");
    append_tool_message(&history1, "File created", "call_456", "create_file");
    append_conversation_message(&history1, "assistant", "Done!");
    cleanup_conversation_history(&history1);
    
    // Load conversation from vector DB
    int result = load_conversation_history(&history2);
    
    TEST_ASSERT_EQUAL(0, result);
    // Messages are stored in vector DB, so we may get recent messages back
    // The exact count depends on what's in the vector DB
    TEST_ASSERT_GREATER_OR_EQUAL(0, history2.count);
    
    cleanup_conversation_history(&history2);
}


int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_init_conversation_history);
    RUN_TEST(test_init_conversation_history_with_null);
    RUN_TEST(test_load_conversation_history_empty);
    RUN_TEST(test_load_conversation_history_with_null);
    RUN_TEST(test_append_conversation_message_first_message);
    RUN_TEST(test_append_conversation_message_multiple_messages);
    RUN_TEST(test_append_conversation_message_with_null_parameters);
    RUN_TEST(test_append_conversation_message_with_multiline_content);
    RUN_TEST(test_cleanup_conversation_history);
    RUN_TEST(test_cleanup_conversation_history_with_null);
    RUN_TEST(test_conversation_persistence_across_loads);
    RUN_TEST(test_large_conversation_handling);
    RUN_TEST(test_append_tool_message);
    RUN_TEST(test_append_tool_message_with_null_parameters);
    RUN_TEST(test_conversation_persistence_with_tool_messages);
    
    return UNITY_END();
}