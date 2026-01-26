#include "../unity/unity.h"
#include "../../src/session/conversation_tracker.h"
#include "../../src/db/document_store.h"
#include "../../src/utils/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <cJSON.h>
#include "../../src/utils/ralph_home.h"

void setUp(void) {
    ralph_home_init(NULL);
    // Initialize config to load API key from environment
    config_init();
    // Clear any existing conversation data
    document_store_clear_conversations();
}

void tearDown(void) {
    // Clear conversation data after each test to prevent interference
    document_store_clear_conversations();
    // Cleanup config
    config_cleanup();

    ralph_home_cleanup();
}

/**
 * Test that assistant messages with tool_calls don't have tool_calls stored in memory.
 *
 * When ralph stores conversation history to the vector database (for long-term memory),
 * only the user messages and assistant's final response content should be stored.
 * Tool calls are ephemeral implementation details that don't need to be remembered.
 */
void test_assistant_tool_calls_not_stored_in_vector_db(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add a user message
    append_conversation_message(&history, "user", "What's the weather like in London?");

    // Add an assistant message in GPT format with tool_calls
    // This is what format_model_assistant_tool_message creates for GPT models
    const char* assistant_with_tool_calls =
        "{\"role\": \"assistant\", \"content\": null, \"tool_calls\": [{\"id\": \"call_abc123\", \"type\": \"function\", \"function\": {\"name\": \"get_weather\", \"arguments\": \"{\\\"location\\\": \\\"London\\\"}\"}}]}";

    append_conversation_message(&history, "assistant", assistant_with_tool_calls);

    // Give some time for processing
    usleep(100000); // 100ms

    // Add a final assistant response (no tool_calls)
    append_conversation_message(&history, "assistant", "The weather in London is sunny and 22 degrees.");

    // Clear in-memory history and reload from vector database
    cleanup_conversation_history(&history);

    // Give some time for the database to sync
    usleep(100000); // 100ms

    ConversationHistory loaded;
    init_conversation_history(&loaded);

    int result = load_conversation_history(&loaded);
    TEST_ASSERT_EQUAL_INT(0, result);

    printf("Loaded %zu messages from conversation history\n", loaded.count);

    // Check each assistant message - none should contain tool_calls
    for (size_t i = 0; i < loaded.count; i++) {
        printf("Message %zu: role=%s\n", i, loaded.data[i].role);

        if (strcmp(loaded.data[i].role, "assistant") == 0) {
            const char* content = loaded.data[i].content;
            printf("  content: %s\n", content);

            // The stored content should NOT contain "tool_calls"
            TEST_ASSERT_NULL_MESSAGE(
                strstr(content, "tool_calls"),
                "Assistant message in memory should not contain tool_calls"
            );

            // If it's JSON, parse it and verify no tool_calls key
            cJSON* json = cJSON_Parse(content);
            if (json) {
                cJSON* tool_calls = cJSON_GetObjectItem(json, "tool_calls");
                TEST_ASSERT_NULL_MESSAGE(
                    tool_calls,
                    "Assistant message JSON should not have tool_calls key"
                );
                cJSON_Delete(json);
            }
        }
    }

    cleanup_conversation_history(&loaded);
}

/**
 * Test that assistant messages with actual content plus tool_calls
 * only store the content, not the tool_calls.
 */
void test_assistant_content_with_tool_calls_stores_only_content(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add a user message
    append_conversation_message(&history, "user", "Help me check the weather");

    // Add an assistant message with both content AND tool_calls
    const char* assistant_with_content_and_tools =
        "{\"role\": \"assistant\", \"content\": \"Let me check the weather for you.\", \"tool_calls\": [{\"id\": \"call_xyz789\", \"type\": \"function\", \"function\": {\"name\": \"get_weather\", \"arguments\": \"{\\\"location\\\": \\\"Paris\\\"}\"}}]}";

    append_conversation_message(&history, "assistant", assistant_with_content_and_tools);

    usleep(100000); // 100ms

    // Clear and reload
    cleanup_conversation_history(&history);

    usleep(100000); // 100ms

    ConversationHistory loaded;
    init_conversation_history(&loaded);

    int result = load_conversation_history(&loaded);
    TEST_ASSERT_EQUAL_INT(0, result);

    printf("Loaded %zu messages\n", loaded.count);

    // Find the assistant message and verify only content is stored
    int found_assistant = 0;
    for (size_t i = 0; i < loaded.count; i++) {
        if (strcmp(loaded.data[i].role, "assistant") == 0) {
            found_assistant = 1;
            const char* content = loaded.data[i].content;
            printf("Assistant content: %s\n", content);

            // Should NOT contain tool_calls
            TEST_ASSERT_NULL_MESSAGE(
                strstr(content, "tool_calls"),
                "Stored assistant message should not contain tool_calls"
            );

            // SHOULD contain the actual message content
            TEST_ASSERT_NOT_NULL_MESSAGE(
                strstr(content, "Let me check the weather"),
                "Stored assistant message should contain the actual content"
            );
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(found_assistant, "Should have found an assistant message");

    cleanup_conversation_history(&loaded);
}

/**
 * Test that assistant messages with only tool_calls (null content)
 * are not stored at all in the vector database.
 */
void test_assistant_with_only_tool_calls_not_stored(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    // Add a user message
    append_conversation_message(&history, "user", "Check the temperature");

    usleep(100000);

    // Add an assistant message with ONLY tool_calls (null/empty content)
    // These are pure tool invocation messages with no user-facing content
    const char* tool_only_message =
        "{\"role\": \"assistant\", \"content\": null, \"tool_calls\": [{\"id\": \"call_temp123\", \"type\": \"function\", \"function\": {\"name\": \"get_temp\", \"arguments\": \"{}\"}}]}";

    append_conversation_message(&history, "assistant", tool_only_message);

    usleep(100000);

    // Add a meaningful final response
    append_conversation_message(&history, "assistant", "The temperature is 20 degrees.");

    // Clear and reload
    cleanup_conversation_history(&history);

    usleep(100000);

    ConversationHistory loaded;
    init_conversation_history(&loaded);

    int result = load_conversation_history(&loaded);
    TEST_ASSERT_EQUAL_INT(0, result);

    printf("Loaded %zu messages\n", loaded.count);

    // Count meaningful assistant messages (should only be the final one)
    int meaningful_assistant_count = 0;
    for (size_t i = 0; i < loaded.count; i++) {
        if (strcmp(loaded.data[i].role, "assistant") == 0) {
            const char* content = loaded.data[i].content;
            printf("Assistant message: %s\n", content);

            // Should not be a tool-only message
            if (strstr(content, "tool_calls") == NULL) {
                meaningful_assistant_count++;
            }
        }
    }

    // We should only have 1 meaningful assistant message (the final response)
    // The tool-only message should not be stored
    printf("Found %d meaningful assistant messages\n", meaningful_assistant_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, meaningful_assistant_count,
        "Only meaningful assistant responses should be stored, not tool-only messages");

    cleanup_conversation_history(&loaded);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_assistant_tool_calls_not_stored_in_vector_db);
    RUN_TEST(test_assistant_content_with_tool_calls_stores_only_content);
    RUN_TEST(test_assistant_with_only_tool_calls_not_stored);
    return UNITY_END();
}
