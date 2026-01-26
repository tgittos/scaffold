#include "unity.h"
#include "conversation_compactor.h"
#include "conversation_tracker.h"
#include "ralph.h"
#include "../src/db/document_store.h"
#include <string.h>
#include <stdlib.h>
#include "../src/utils/ralph_home.h"

void setUp(void) {
    ralph_home_init(NULL);
    // Clear conversation data to ensure test isolation
    document_store_clear_conversations();
}

void tearDown(void) {
    // This is run after each test

    ralph_home_cleanup();
}

void test_compaction_config_init(void) {
    CompactionConfig config;
    compaction_config_init(&config);
    
    TEST_ASSERT_EQUAL_INT(10, config.preserve_recent_messages);
    TEST_ASSERT_EQUAL_INT(5, config.preserve_recent_tools);
    TEST_ASSERT_GREATER_THAN_INT(0, config.background_threshold);
}

void test_should_background_compact_too_few_messages(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add only 3 messages - not enough to trigger background trimming
    append_conversation_message(&conversation, "user", "Hello");
    append_conversation_message(&conversation, "assistant", "Hi there");
    append_conversation_message(&conversation, "user", "How are you?");
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    int should_compact = should_background_compact(&conversation, &config, 10000);
    TEST_ASSERT_EQUAL_INT(0, should_compact);  // Should not compact
    
    cleanup_conversation_history(&conversation);
}

void test_should_background_compact_tokens_within_limit(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add enough messages
    for (int i = 0; i < 20; i++) {
        append_conversation_message(&conversation, "user", "Message content");
        append_conversation_message(&conversation, "assistant", "Response content");
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    // Current tokens below threshold - should not compact
    int should_compact = should_background_compact(&conversation, &config, 1000);
    TEST_ASSERT_EQUAL_INT(0, should_compact);  // Should not compact
    
    cleanup_conversation_history(&conversation);
}

void test_should_background_compact_needs_compaction(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add enough messages
    for (int i = 0; i < 20; i++) {
        append_conversation_message(&conversation, "user", "Message content");
        append_conversation_message(&conversation, "assistant", "Response content");
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    // Current tokens above threshold - should compact
    int should_compact = should_background_compact(&conversation, &config, config.background_threshold + 1000);
    TEST_ASSERT_EQUAL_INT(1, should_compact);  // Should compact
    
    cleanup_conversation_history(&conversation);
}

void test_cleanup_compaction_result(void) {
    CompactionResult result = {0};
    
    // Initialize with some test values
    result.messages_trimmed = 5;
    result.messages_after_trimming = 10;
    result.tokens_saved = 100;
    
    cleanup_compaction_result(&result);
    
    TEST_ASSERT_EQUAL_INT(0, result.messages_trimmed);
    TEST_ASSERT_EQUAL_INT(0, result.messages_after_trimming);
    TEST_ASSERT_EQUAL_INT(0, result.tokens_saved);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_compaction_config_init);
    RUN_TEST(test_should_background_compact_too_few_messages);
    RUN_TEST(test_should_background_compact_tokens_within_limit);
    RUN_TEST(test_should_background_compact_needs_compaction);
    RUN_TEST(test_cleanup_compaction_result);
    return UNITY_END();
}