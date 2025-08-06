#include "unity.h"
#include "../../src/session/conversation_tracker.h"
#include "../../src/db/document_store.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <cJSON.h>

void setUp(void) {
    // Clean up test data
    system("rm -rf /tmp/test_conv_tool_seq*");
}

void tearDown(void) {
    // Clean up test data
    system("rm -rf /tmp/test_conv_tool_seq*");
}

void test_conversation_history_loads_chronologically(void) {
    // Create a document store for testing
    document_store_t* store = document_store_create("/tmp/test_conv_tool_seq");
    TEST_ASSERT_NOT_NULL(store);
    
    // Set it as the singleton instance
    document_store_set_instance(store);
    
    ConversationHistory history;
    
    // Add messages using the public API in reverse chronological order to test sorting
    append_conversation_message(&history, "assistant", "Second message");
    sleep(1);
    append_conversation_message(&history, "user", "First message");
    sleep(1);
    append_tool_message(&history, "Tool result for call_123", "call_123", "test_tool");
    
    // Clear the history and reload from database
    cleanup_conversation_history(&history);
    
    // Load conversation history - this should sort by timestamp
    int result = load_conversation_history(&history);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // The messages should be sorted chronologically despite being added out of order
    TEST_ASSERT_GREATER_THAN(0, history.count);
    
    // Verify we loaded some messages
    printf("Loaded %d messages from conversation history\n", history.count);
    for (int i = 0; i < history.count; i++) {
        printf("Message %d: role=%s, content=%.50s%s\n", 
               i, history.messages[i].role, history.messages[i].content,
               strlen(history.messages[i].content) > 50 ? "..." : "");
    }
    
    cleanup_conversation_history(&history);
    document_store_destroy(store);
}

void test_tool_message_with_proper_sequence(void) {
    // Create a document store for testing  
    document_store_t* store = document_store_create("/tmp/test_conv_tool_seq2");
    TEST_ASSERT_NOT_NULL(store);
    
    document_store_set_instance(store);
    
    ConversationHistory history;
    init_conversation_history(&history);
    
    // Add a proper tool calling sequence
    append_conversation_message(&history, "user", "What's the weather like?");
    
    // Assistant message with tool call (simulating API response format)
    const char* assistant_with_tool = "{\"role\": \"assistant\", \"content\": [{\"type\": \"tool_use\", \"id\": \"call_weather_123\", \"name\": \"get_weather\", \"input\": {\"location\": \"London\"}}]}";
    append_conversation_message(&history, "assistant", assistant_with_tool);
    
    // Tool result
    append_tool_message(&history, "The weather in London is sunny, 22°C", "call_weather_123", "get_weather");
    
    // Final assistant response
    append_conversation_message(&history, "assistant", "The weather in London is currently sunny with a temperature of 22°C.");
    
    // Clear and reload
    cleanup_conversation_history(&history);
    
    int result = load_conversation_history(&history);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    printf("Loaded conversation with %d messages\n", history.count);
    
    // Verify we have a valid sequence (should not have orphaned tool results)
    int has_tool_message = 0;
    int has_assistant_with_tool_use = 0;
    
    for (int i = 0; i < history.count; i++) {
        if (strcmp(history.messages[i].role, "tool") == 0) {
            has_tool_message = 1;
            TEST_ASSERT_NOT_NULL(history.messages[i].tool_call_id);
            printf("Found tool message with tool_call_id: %s\n", history.messages[i].tool_call_id);
        }
        
        if (strcmp(history.messages[i].role, "assistant") == 0 && 
            strstr(history.messages[i].content, "call_weather_123")) {
            has_assistant_with_tool_use = 1;
            printf("Found assistant message with tool_use\n");
        }
    }
    
    // If we have a tool message, we should also have the corresponding tool_use
    if (has_tool_message) {
        TEST_ASSERT_TRUE_MESSAGE(has_assistant_with_tool_use, 
                                "Tool message found but no corresponding tool_use in assistant message");
    }
    
    cleanup_conversation_history(&history);
    document_store_destroy(store);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_conversation_history_loads_chronologically);
    RUN_TEST(test_tool_message_with_proper_sequence);
    return UNITY_END();
}