#include "api_common.h"
#include "conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test that reproduces the Anthropic tool_result missing bug

int test_anthropic_tool_sequence_formatting() {
    printf("Testing Anthropic tool sequence formatting...\n");
    
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
    
    printf("Conversation has %d messages:\n", history.count);
    for (int i = 0; i < history.count; i++) {
        const ConversationMessage* msg = &history.messages[i];
        printf("  %d: role=%s, tool_call_id=%s\n", i, 
               msg->role ? msg->role : "NULL", 
               msg->tool_call_id ? msg->tool_call_id : "NULL");
        
        int result = format_anthropic_message(buffer, sizeof(buffer), msg, message_count == 0);
        if (result < 0) {
            printf("ERROR: Failed to format message %d (role=%s)\n", i, msg->role);
            cleanup_conversation_history(&history);
            return -1;
        }
        
        printf("  Formatted as: %s\n", buffer);
        message_count++;
    }
    
    cleanup_conversation_history(&history);
    printf("PASS: All messages formatted successfully\n");
    return 0;
}

int test_build_anthropic_messages_json() {
    printf("Testing build_anthropic_messages_json...\n");
    
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
    
    if (result < 0) {
        printf("ERROR: build_anthropic_messages_json failed\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    printf("Generated messages JSON (%d chars):\n%s\n", result, messages_buffer);
    
    // Check if tool_result is present in the JSON
    if (strstr(messages_buffer, "tool_result") == NULL) {
        printf("ERROR: tool_result missing from messages JSON!\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    // Check if tool_use_id is present
    if (strstr(messages_buffer, "toolu_test123") == NULL) {
        printf("ERROR: tool_use_id missing from messages JSON!\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    cleanup_conversation_history(&history);
    printf("PASS: Messages JSON contains tool_result and tool_use_id\n");
    return 0;
}

int main() {
    printf("Testing Anthropic API tool result bug...\n");
    
    int result1 = test_anthropic_tool_sequence_formatting();
    int result2 = test_build_anthropic_messages_json();
    
    if (result1 == 0 && result2 == 0) {
        printf("All tests passed - tool result formatting works correctly\n");
        return 0;
    } else {
        printf("Tests failed - tool result formatting has issues\n");
        return 1;
    }
}