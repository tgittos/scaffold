#include "unity.h"
#include "api_common.h"
#include "conversation_tracker.h"
#include "../src/db/document_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
    // Clear conversation data to ensure test isolation
    document_store_clear_conversations();
}

void tearDown(void) {
    // Clean up after each test
}

void test_anthropic_tool_sequence_formatting(void) {
    // Create conversation history that mimics the problematic scenario
    ConversationHistory history = {0};
    init_conversation_history(&history);
    
    // Add user message
    append_conversation_message(&history, "user", "read the Makefile file");
    
    // Add assistant message with tool_use (raw JSON from API)
    const char* raw_anthropic_response = "{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-sonnet-4\",\"content\":[{\"type\":\"text\",\"text\":\"I'll read the Makefile for you.\"},{\"type\":\"tool_use\",\"id\":\"toolu_test123\",\"name\":\"file_read\",\"input\":{\"file_path\":\"Makefile\"}}],\"stop_reason\":\"tool_use\"}";
    append_conversation_message(&history, "assistant", raw_anthropic_response);
    
    // Add tool result
    append_tool_message(&history, "{\"success\": true, \"content\": \"makefile content\"}", "toolu_test123", "file_read");
    
    // Add final assistant response
    append_conversation_message(&history, "assistant", "This is the final response after reading the Makefile");
    
    // Now format the messages for Anthropic API
    char buffer[8192];
    int message_count = 0;
    
    TEST_ASSERT_EQUAL(4, history.count);
    
    for (int i = 0; i < history.count; i++) {
        const ConversationMessage* msg = &history.messages[i];
        
        int result = format_anthropic_message(buffer, sizeof(buffer), msg, message_count == 0);
        TEST_ASSERT_TRUE(result >= 0);
        
        message_count++;
    }
    
    cleanup_conversation_history(&history);
}

void test_build_anthropic_messages_json(void) {
    // Create conversation history that mimics the problematic scenario
    ConversationHistory history = {0};
    init_conversation_history(&history);
    
    // Add user message
    append_conversation_message(&history, "user", "read the Makefile file");
    
    // Add assistant message with tool_use (raw JSON from API)
    const char* raw_anthropic_response = "{\"id\":\"msg_test\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-sonnet-4\",\"content\":[{\"type\":\"text\",\"text\":\"I'll read the Makefile for you.\"},{\"type\":\"tool_use\",\"id\":\"toolu_test123\",\"name\":\"file_read\",\"input\":{\"file_path\":\"Makefile\"}}],\"stop_reason\":\"tool_use\"}";
    append_conversation_message(&history, "assistant", raw_anthropic_response);
    
    // Add tool result
    append_tool_message(&history, "{\"success\": true, \"content\": \"makefile content\"}", "toolu_test123", "file_read");
    
    // Add final assistant response
    append_conversation_message(&history, "assistant", "This is the final response after reading the Makefile");
    
    // Now build the full messages JSON using the same function that's failing
    char messages_buffer[16384];
    int result = build_anthropic_messages_json(messages_buffer, sizeof(messages_buffer),
                                             NULL, &history, "second user message",
                                             format_anthropic_message, 1);
    
    TEST_ASSERT_TRUE(result >= 0);
    
    // Check if tool_result is present in the JSON
    TEST_ASSERT_NOT_NULL(strstr(messages_buffer, "tool_result"));
    
    // Check if tool_use_id is present
    TEST_ASSERT_NOT_NULL(strstr(messages_buffer, "toolu_test123"));
    
    cleanup_conversation_history(&history);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_anthropic_tool_sequence_formatting);
    RUN_TEST(test_build_anthropic_messages_json);
    return UNITY_END();
}