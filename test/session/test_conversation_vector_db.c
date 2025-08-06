#include "../unity/unity.h"
#include "../../src/session/conversation_tracker.h"
#include "../../src/db/document_store.h"
#include "../../src/utils/config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void setUp(void) {
    // Initialize config to load API key from environment
    config_init();
}

void tearDown(void) {
    // Cleanup config
    config_cleanup();
}

void test_conversation_stored_in_vector_db(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add messages
    int result1 = append_conversation_message(&history, "user", "Hello from vector DB test");
    int result2 = append_conversation_message(&history, "assistant", "Hello! This response is stored in vector DB");
    
    TEST_ASSERT_EQUAL(0, result1);
    TEST_ASSERT_EQUAL(0, result2);
    TEST_ASSERT_EQUAL(2, history.count);
    
    // Give the vector DB time to process
    usleep(100000); // 100ms delay
    
    cleanup_conversation_history(&history);
    
    // Now load conversation from vector DB
    ConversationHistory loaded_history;
    init_conversation_history(&loaded_history);
    
    int load_result = load_conversation_history(&loaded_history);
    TEST_ASSERT_EQUAL(0, load_result);
    
    // Should have loaded the messages from vector DB
    TEST_ASSERT_GREATER_OR_EQUAL(2, loaded_history.count);
    
    // Check that our messages are in the loaded history
    int found_user_msg = 0;
    int found_assistant_msg = 0;
    
    for (int i = 0; i < loaded_history.count; i++) {
        if (strcmp(loaded_history.messages[i].role, "user") == 0 &&
            strstr(loaded_history.messages[i].content, "Hello from vector DB test") != NULL) {
            found_user_msg = 1;
        }
        if (strcmp(loaded_history.messages[i].role, "assistant") == 0 &&
            strstr(loaded_history.messages[i].content, "stored in vector DB") != NULL) {
            found_assistant_msg = 1;
        }
    }
    
    TEST_ASSERT_TRUE(found_user_msg);
    TEST_ASSERT_TRUE(found_assistant_msg);
    
    cleanup_conversation_history(&loaded_history);
}

void test_extended_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add some messages
    append_conversation_message(&history, "user", "First message");
    append_conversation_message(&history, "assistant", "First response");
    append_conversation_message(&history, "user", "Second message");
    append_conversation_message(&history, "assistant", "Second response");
    
    cleanup_conversation_history(&history);
    
    // Load extended history
    ConversationHistory extended_history;
    init_conversation_history(&extended_history);
    
    int result = load_extended_conversation_history(&extended_history, 7, 100);
    TEST_ASSERT_EQUAL(0, result);
    
    // Should have loaded messages
    TEST_ASSERT_GREATER_OR_EQUAL(4, extended_history.count);
    
    cleanup_conversation_history(&extended_history);
}

void test_search_conversation_history(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add messages with specific keywords
    append_conversation_message(&history, "user", "Tell me about quantum physics");
    append_conversation_message(&history, "assistant", "Quantum physics is the study of matter at atomic scales");
    append_conversation_message(&history, "user", "What about classical mechanics?");
    append_conversation_message(&history, "assistant", "Classical mechanics deals with macroscopic objects");
    
    cleanup_conversation_history(&history);
    
    // Search for quantum-related messages
    ConversationHistory* search_results = search_conversation_history("quantum", 10);
    
    if (search_results != NULL) {
        // Should find at least the quantum-related messages
        TEST_ASSERT_GREATER_OR_EQUAL(1, search_results->count);
        
        // Check that results contain quantum
        int found_quantum = 0;
        for (int i = 0; i < search_results->count; i++) {
            if (strstr(search_results->messages[i].content, "quantum") != NULL ||
                strstr(search_results->messages[i].content, "Quantum") != NULL) {
                found_quantum = 1;
                break;
            }
        }
        TEST_ASSERT_TRUE(found_quantum);
        
        cleanup_conversation_history(search_results);
        free(search_results);
    }
}

void test_tool_messages_in_vector_db(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add a tool message
    int result = append_tool_message(&history, "File created successfully", "tool_123", "file_write");
    
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, history.count);
    TEST_ASSERT_EQUAL_STRING("tool", history.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("tool_123", history.messages[0].tool_call_id);
    TEST_ASSERT_EQUAL_STRING("file_write", history.messages[0].tool_name);
    
    cleanup_conversation_history(&history);
    
    // Load and verify tool message from vector DB
    ConversationHistory loaded_history;
    init_conversation_history(&loaded_history);
    
    int load_result = load_conversation_history(&loaded_history);
    TEST_ASSERT_EQUAL(0, load_result);
    
    // Find the tool message
    int found_tool_msg = 0;
    for (int i = 0; i < loaded_history.count; i++) {
        if (strcmp(loaded_history.messages[i].role, "tool") == 0 &&
            loaded_history.messages[i].tool_call_id != NULL &&
            strcmp(loaded_history.messages[i].tool_call_id, "tool_123") == 0) {
            found_tool_msg = 1;
            TEST_ASSERT_EQUAL_STRING("file_write", loaded_history.messages[i].tool_name);
            break;
        }
    }
    
    TEST_ASSERT_TRUE(found_tool_msg);
    
    cleanup_conversation_history(&loaded_history);
}

void test_sliding_window_retrieval(void) {
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add many messages to test sliding window
    for (int i = 0; i < 30; i++) {
        char msg[100];
        snprintf(msg, sizeof(msg), "Message %d", i);
        append_conversation_message(&history, i % 2 == 0 ? "user" : "assistant", msg);
    }
    
    cleanup_conversation_history(&history);
    
    // Load with sliding window (should get most recent messages)
    ConversationHistory windowed_history;
    init_conversation_history(&windowed_history);
    
    int result = load_conversation_history(&windowed_history);
    TEST_ASSERT_EQUAL(0, result);
    
    // Should have loaded up to SLIDING_WINDOW_SIZE messages (20)
    TEST_ASSERT_LESS_OR_EQUAL(20, windowed_history.count);
    
    cleanup_conversation_history(&windowed_history);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_conversation_stored_in_vector_db);
    RUN_TEST(test_extended_conversation_history);
    RUN_TEST(test_search_conversation_history);
    RUN_TEST(test_tool_messages_in_vector_db);
    RUN_TEST(test_sliding_window_retrieval);
    
    return UNITY_END();
}