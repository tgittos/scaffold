#include "conversation_tracker.h"
#include "api_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Test loading the actual CONVERSATION.md file and verifying all messages are loaded correctly
int test_conversation_loading_from_file() {
    printf("Testing conversation loading from CONVERSATION.md...\n");
    
    ConversationHistory history = {0};
    
    // Load the actual CONVERSATION.md file
    int result = load_conversation_history(&history);
    if (result != 0) {
        printf("ERROR: Failed to load conversation history\n");
        return -1;
    }
    
    printf("Loaded %d messages from CONVERSATION.md:\n", history.count);
    for (int i = 0; i < history.count; i++) {
        const ConversationMessage* msg = &history.messages[i];
        printf("  Message %d:\n", i);
        printf("    role: %s\n", msg->role ? msg->role : "NULL");
        printf("    tool_call_id: %s\n", msg->tool_call_id ? msg->tool_call_id : "NULL");
        printf("    tool_name: %s\n", msg->tool_name ? msg->tool_name : "NULL");
        printf("    content (first 100 chars): %.100s...\n", msg->content ? msg->content : "NULL");
        printf("\n");
    }
    
    // Verify we have the expected tool message
    int tool_message_found = 0;
    for (int i = 0; i < history.count; i++) {
        if (history.messages[i].role && strcmp(history.messages[i].role, "tool") == 0) {
            tool_message_found = 1;
            printf("Found tool message at index %d with tool_call_id: %s\n", 
                   i, history.messages[i].tool_call_id ? history.messages[i].tool_call_id : "NULL");
            break;
        }
    }
    
    if (!tool_message_found) {
        printf("ERROR: No tool message found in loaded conversation!\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    // Now test building the API messages
    char messages_buffer[16384];
    int api_result = build_anthropic_messages_json(messages_buffer, sizeof(messages_buffer),
                                                  NULL, &history, "follow up message",
                                                  format_anthropic_message, 1);
    
    if (api_result < 0) {
        printf("ERROR: build_anthropic_messages_json failed\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    printf("Generated API messages JSON (%d chars):\n%s\n", api_result, messages_buffer);
    
    // Check if tool_result is present in the JSON
    if (strstr(messages_buffer, "tool_result") == NULL) {
        printf("ERROR: tool_result missing from API messages JSON!\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    // Check if the tool_call_id is present 
    if (strstr(messages_buffer, "toolu_01CjJSmVt5omZK5Gz4wTypgz") == NULL) {
        printf("ERROR: Expected tool_call_id missing from API messages JSON!\n");
        cleanup_conversation_history(&history);
        return -1;
    }
    
    cleanup_conversation_history(&history);
    printf("PASS: Conversation loading and API formatting working correctly\n");
    return 0;
}

int main() {
    printf("Testing conversation loading with real CONVERSATION.md file...\n");
    
    int result = test_conversation_loading_from_file();
    
    if (result == 0) {
        printf("All tests passed - conversation loading works correctly\n");
        return 0;
    } else {
        printf("Tests failed - conversation loading has issues\n");
        return 1;
    }
}