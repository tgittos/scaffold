#include "unity.h"
#include "conversation_tracker.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // Clean up any existing test files
    unlink("CONVERSATION.md");
    unlink("test_conversation.md");
}

void tearDown(void) {
    // Clean up test files
    unlink("CONVERSATION.md");
    unlink("test_conversation.md");
}

void test_init_conversation_history(void) {
    ConversationHistory history;
    
    init_conversation_history(&history);
    
    TEST_ASSERT_NULL(history.messages);
    TEST_ASSERT_EQUAL(0, history.count);
    TEST_ASSERT_EQUAL(0, history.capacity);
}

void test_init_conversation_history_with_null(void) {
    // Should not crash
    init_conversation_history(NULL);
    TEST_ASSERT_TRUE(1); // Just to have an assertion
}

void test_load_conversation_history_no_file(void) {
    ConversationHistory history;
    
    // Ensure file doesn't exist
    unlink("CONVERSATION.md");
    
    int result = load_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(0, history.count);
    TEST_ASSERT_NULL(history.messages);
    
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
    TEST_ASSERT_NOT_NULL(history.messages);
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("Hello, how are you?", history.messages[0].content);
    
    // Check that file was created
    FILE *file = fopen("CONVERSATION.md", "r");
    TEST_ASSERT_NOT_NULL(file);
    if (file) fclose(file);
    
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
    
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("What is 2+2?", history.messages[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.messages[1].role);
    TEST_ASSERT_EQUAL_STRING("2+2 equals 4.", history.messages[1].content);
    
    TEST_ASSERT_EQUAL_STRING("user", history.messages[2].role);
    TEST_ASSERT_EQUAL_STRING("Thank you!", history.messages[2].content);
    
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
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING(multiline_content, history.messages[0].content);
    
    cleanup_conversation_history(&history);
}

void test_load_conversation_history_from_file(void) {
    // Create a test conversation file
    FILE *file = fopen("CONVERSATION.md", "w");
    TEST_ASSERT_NOT_NULL(file);
    
    fprintf(file, "\n## User:\nHello there!\n\n## Assistant:\nHi! How can I help you?\n\n## User:\nWhat is the weather like?\n");
    fclose(file);
    
    ConversationHistory history;
    int result = load_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(3, history.count);
    
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("Hello there!", history.messages[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.messages[1].role);
    TEST_ASSERT_EQUAL_STRING("Hi! How can I help you?", history.messages[1].content);
    
    TEST_ASSERT_EQUAL_STRING("user", history.messages[2].role);
    TEST_ASSERT_EQUAL_STRING("What is the weather like?", history.messages[2].content);
    
    cleanup_conversation_history(&history);
}

void test_load_conversation_history_with_escaped_newlines(void) {
    // Create a test conversation file with escaped newlines
    FILE *file = fopen("CONVERSATION.md", "w");
    TEST_ASSERT_NOT_NULL(file);
    
    fprintf(file, "\n## User:\nThis is line 1\\nThis is line 2\n\n## Assistant:\nMultiline response:\\nLine A\\nLine B\n");
    fclose(file);
    
    ConversationHistory history;
    int result = load_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, history.count);
    
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("This is line 1\nThis is line 2", history.messages[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.messages[1].role);
    TEST_ASSERT_EQUAL_STRING("Multiline response:\nLine A\nLine B", history.messages[1].content);
    
    cleanup_conversation_history(&history);
}

void test_load_conversation_history_with_empty_content(void) {
    // Create a test conversation file with empty content
    FILE *file = fopen("CONVERSATION.md", "w");
    TEST_ASSERT_NOT_NULL(file);
    
    fprintf(file, "\n## User:\n\n## Assistant:\nResponse to empty message\n");
    fclose(file);
    
    ConversationHistory history;
    int result = load_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, history.count);
    
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("", history.messages[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.messages[1].role);
    TEST_ASSERT_EQUAL_STRING("Response to empty message", history.messages[1].content);
    
    cleanup_conversation_history(&history);
}

void test_cleanup_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add some messages
    append_conversation_message(&history, "user", "Test message 1");
    append_conversation_message(&history, "assistant", "Test response 1");
    
    TEST_ASSERT_EQUAL(2, history.count);
    TEST_ASSERT_NOT_NULL(history.messages);
    
    cleanup_conversation_history(&history);
    
    TEST_ASSERT_EQUAL(0, history.count);
    TEST_ASSERT_EQUAL(0, history.capacity);
    TEST_ASSERT_NULL(history.messages);
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
    
    // Load conversation from file
    int result = load_conversation_history(&history2);
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, history2.count);
    TEST_ASSERT_EQUAL_STRING("user", history2.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("First message", history2.messages[0].content);
    TEST_ASSERT_EQUAL_STRING("assistant", history2.messages[1].role);
    TEST_ASSERT_EQUAL_STRING("First response", history2.messages[1].content);
    
    // Add more messages
    append_conversation_message(&history2, "user", "Second message");
    
    TEST_ASSERT_EQUAL(3, history2.count);
    TEST_ASSERT_EQUAL_STRING("user", history2.messages[2].role);
    TEST_ASSERT_EQUAL_STRING("Second message", history2.messages[2].content);
    
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
    TEST_ASSERT_EQUAL_STRING("user", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("User message 0", history.messages[0].content);
    
    TEST_ASSERT_EQUAL_STRING("assistant", history.messages[99].role);
    TEST_ASSERT_EQUAL_STRING("Assistant response 49", history.messages[99].content);
    
    cleanup_conversation_history(&history);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_init_conversation_history);
    RUN_TEST(test_init_conversation_history_with_null);
    RUN_TEST(test_load_conversation_history_no_file);
    RUN_TEST(test_load_conversation_history_with_null);
    RUN_TEST(test_append_conversation_message_first_message);
    RUN_TEST(test_append_conversation_message_multiple_messages);
    RUN_TEST(test_append_conversation_message_with_null_parameters);
    RUN_TEST(test_append_conversation_message_with_multiline_content);
    RUN_TEST(test_load_conversation_history_from_file);
    RUN_TEST(test_load_conversation_history_with_escaped_newlines);
    RUN_TEST(test_load_conversation_history_with_empty_content);
    RUN_TEST(test_cleanup_conversation_history);
    RUN_TEST(test_cleanup_conversation_history_with_null);
    RUN_TEST(test_conversation_persistence_across_loads);
    RUN_TEST(test_large_conversation_handling);
    
    return UNITY_END();
}