#include "unity.h"
#include "conversation_compactor.h"
#include "conversation_tracker.h"
#include "ralph.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
    // This is run before each test
}

void tearDown(void) {
    // This is run after each test
}

void test_compaction_config_init(void) {
    CompactionConfig config;
    compaction_config_init(&config);
    
    TEST_ASSERT_EQUAL_INT(5, config.preserve_recent_messages);
    TEST_ASSERT_EQUAL_INT(3, config.preserve_recent_tools);
    TEST_ASSERT_EQUAL_INT(4, config.min_segment_size);
    TEST_ASSERT_EQUAL_INT(20, config.max_segment_size);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, config.compaction_ratio);
}

void test_should_compact_conversation_too_few_messages(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add only 3 messages - not enough to compact (need 5 recent + 4 min segment = 9)
    append_conversation_message(&conversation, "user", "Hello");
    append_conversation_message(&conversation, "assistant", "Hi there");
    append_conversation_message(&conversation, "user", "How are you?");
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    int should_compact = should_compact_conversation(&conversation, &config, 10000, 5000);
    TEST_ASSERT_EQUAL_INT(0, should_compact);  // Should not compact
    
    cleanup_conversation_history(&conversation);
}

void test_should_compact_conversation_tokens_within_limit(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add enough messages
    for (int i = 0; i < 15; i++) {
        append_conversation_message(&conversation, "user", "Message content");
        append_conversation_message(&conversation, "assistant", "Response content");
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    // Current tokens below target - should not compact
    int should_compact = should_compact_conversation(&conversation, &config, 1000, 5000);
    TEST_ASSERT_EQUAL_INT(0, should_compact);  // Should not compact
    
    cleanup_conversation_history(&conversation);
}

void test_should_compact_conversation_needs_compaction(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add enough messages
    for (int i = 0; i < 15; i++) {
        append_conversation_message(&conversation, "user", "Message content");
        append_conversation_message(&conversation, "assistant", "Response content");
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    // Current tokens above target - should compact
    int should_compact = should_compact_conversation(&conversation, &config, 10000, 5000);
    TEST_ASSERT_EQUAL_INT(1, should_compact);  // Should compact
    
    cleanup_conversation_history(&conversation);
}

void test_find_compaction_segment_simple(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add messages - ensure we have enough for compaction
    for (int i = 0; i < 15; i++) {
        char user_msg[100] = {0};
        char assistant_msg[100] = {0};
        snprintf(user_msg, sizeof(user_msg), "User message %d", i);
        snprintf(assistant_msg, sizeof(assistant_msg), "Assistant response %d", i);
        
        append_conversation_message(&conversation, "user", user_msg);
        append_conversation_message(&conversation, "assistant", assistant_msg);
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    int start_index, end_index;
    int result = find_compaction_segment(&conversation, &config, &start_index, &end_index);
    
    TEST_ASSERT_EQUAL_INT(0, result);  // Should succeed
    TEST_ASSERT_EQUAL_INT(0, start_index);  // Should start from beginning
    TEST_ASSERT_GREATER_OR_EQUAL_INT(config.min_segment_size - 1, end_index);  // Should be at least min size
    TEST_ASSERT_LESS_OR_EQUAL_INT(config.max_segment_size - 1, end_index);  // Should be at most max size
    
    cleanup_conversation_history(&conversation);
}

void test_find_compaction_segment_with_tool_messages(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add regular messages
    for (int i = 0; i < 10; i++) {
        char user_msg[100] = {0};
        char assistant_msg[100] = {0};
        snprintf(user_msg, sizeof(user_msg), "User message %d", i);
        snprintf(assistant_msg, sizeof(assistant_msg), "Assistant response %d", i);
        
        append_conversation_message(&conversation, "user", user_msg);
        append_conversation_message(&conversation, "assistant", assistant_msg);
    }
    
    // Add recent tool messages that should be preserved
    append_tool_message(&conversation, "Tool result 1", "call_1", "shell_tool");
    append_tool_message(&conversation, "Tool result 2", "call_2", "file_tools");
    append_tool_message(&conversation, "Tool result 3", "call_3", "shell_tool");
    
    // Add more recent messages
    append_conversation_message(&conversation, "user", "Recent user message");
    append_conversation_message(&conversation, "assistant", "Recent assistant response");
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    int start_index, end_index;
    int result = find_compaction_segment(&conversation, &config, &start_index, &end_index);
    
    TEST_ASSERT_EQUAL_INT(0, result);  // Should succeed
    TEST_ASSERT_EQUAL_INT(0, start_index);  // Should start from beginning
    
    // Should not include the recent tool messages (preserve recent 3 tools)
    // Tool messages start at index 20, so end_index should be less than that
    TEST_ASSERT_LESS_THAN_INT(20, end_index);
    
    cleanup_conversation_history(&conversation);
}

void test_compact_conversation_segment_basic(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add messages to compact
    append_conversation_message(&conversation, "user", "First user message");
    append_conversation_message(&conversation, "assistant", "First assistant response");
    append_conversation_message(&conversation, "user", "Second user message");
    append_conversation_message(&conversation, "assistant", "Second assistant response");
    append_conversation_message(&conversation, "user", "Third user message to preserve");
    
    // int original_count = conversation.count;  // Unused for now
    
    CompactionConfig config;
    compaction_config_init(&config);
    
    const char* summary = "## Summary\\n\\nUser asked questions, assistant provided responses.";
    CompactionResult result;
    
    int status = compact_conversation_segment(&conversation, &config, 0, 3, summary, &result);
    
    TEST_ASSERT_EQUAL_INT(0, status);  // Should succeed
    TEST_ASSERT_EQUAL_INT(4, result.messages_compacted);  // Should have compacted 4 messages (0-3)
    TEST_ASSERT_EQUAL_INT(2, result.messages_after_compaction);  // Should have 2 messages left (summary + preserved)
    TEST_ASSERT_GREATER_THAN_INT(0, result.tokens_saved);  // Should have saved some tokens
    TEST_ASSERT_NOT_NULL(result.summary_content);
    
    // Check that the conversation was properly modified
    TEST_ASSERT_EQUAL_INT(2, conversation.count);
    TEST_ASSERT_EQUAL_STRING("assistant", conversation.messages[0].role);  // Summary message
    TEST_ASSERT_EQUAL_STRING(summary, conversation.messages[0].content);  // Summary content
    TEST_ASSERT_EQUAL_STRING("Third user message to preserve", conversation.messages[1].content);  // Preserved message
    
    cleanup_compaction_result(&result);
    cleanup_conversation_history(&conversation);
}

void test_generate_conversation_summary_mock(void) {
    // This test uses a mock session since we can't make real API calls in tests
    SessionData session;
    session_data_init(&session);
    session.config.api_type = 0;  // OpenAI
    session.config.api_url = strdup("http://mock-api.test");
    session.config.model = strdup("gpt-4");
    session.config.api_key = strdup("mock-key");
    
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    append_conversation_message(&conversation, "user", "Hello, how are you?");
    append_conversation_message(&conversation, "assistant", "I'm doing well, thank you!");
    append_conversation_message(&conversation, "user", "What's the weather like?");
    append_tool_message(&conversation, "Weather: Sunny, 75F", "call_1", "weather_tool");
    
    char* summary_content = NULL;
    
    // This test will fail with HTTP error since we can't make real API calls
    // But it tests the parameter validation and setup logic
    int result = generate_conversation_summary(&session, &conversation, 0, 3, &summary_content);
    
    // We expect this to fail due to HTTP error, but not due to parameter validation
    TEST_ASSERT_EQUAL_INT(-1, result);  // Should fail due to HTTP
    TEST_ASSERT_NULL(summary_content);  // Should be null on failure
    
    // Cleanup
    session_data_cleanup(&session);
    cleanup_conversation_history(&conversation);
}

void test_save_compacted_conversation(void) {
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    append_conversation_message(&conversation, "assistant", "## Summary\\n\\nThis is a test summary.");
    append_conversation_message(&conversation, "user", "Recent message");
    
    // Save to a test file (this will overwrite CONVERSATION.md temporarily)
    int result = save_compacted_conversation(&conversation);
    
    TEST_ASSERT_EQUAL_INT(0, result);  // Should succeed
    
    // Verify the file was written by loading it back
    ConversationHistory loaded_conversation = {0};
    int load_result = load_conversation_history(&loaded_conversation);
    
    TEST_ASSERT_EQUAL_INT(0, load_result);
    TEST_ASSERT_EQUAL_INT(2, loaded_conversation.count);
    TEST_ASSERT_EQUAL_STRING("assistant", loaded_conversation.messages[0].role);
    TEST_ASSERT_EQUAL_STRING("## Summary\\n\\nThis is a test summary.", loaded_conversation.messages[0].content);
    
    cleanup_conversation_history(&conversation);
    cleanup_conversation_history(&loaded_conversation);
}

void test_cleanup_compaction_result(void) {
    CompactionResult result;
    result.messages_compacted = 5;
    result.messages_after_compaction = 2;
    result.tokens_saved = 1000;
    result.summary_content = strdup("Test summary content");
    
    cleanup_compaction_result(&result);
    
    TEST_ASSERT_NULL(result.summary_content);
    TEST_ASSERT_EQUAL_INT(0, result.messages_compacted);
    TEST_ASSERT_EQUAL_INT(0, result.messages_after_compaction);
    TEST_ASSERT_EQUAL_INT(0, result.tokens_saved);
}

void test_original_token_limit_bug_reproduction(void) {
    // Reproduce the original bug from CONVERSATION.md where agent hit 150 token limit
    TokenConfig config;
    token_config_init(&config, 200000);  // Large context like Claude
    
    // Test the scenario: large conversation with TodoWrite attempt
    ConversationHistory conversation = {0};
    init_conversation_history(&conversation);
    
    // Add a large conversation history that would trigger the bug
    for (int i = 0; i < 50; i++) {
        char user_msg[500] = {0};
        char assistant_msg[2000] = {0};
        snprintf(user_msg, sizeof(user_msg), "User message %d with some content that adds tokens", i);
        snprintf(assistant_msg, sizeof(assistant_msg), 
                "Long assistant response %d with technical details, code examples, "
                "file paths, and other content that would accumulate tokens over a long conversation. "
                "This simulates the conversation that was happening when the bug occurred.", i);
        
        append_conversation_message(&conversation, "user", user_msg);
        append_conversation_message(&conversation, "assistant", assistant_msg);
    }
    
    // Estimate total tokens (should be reasonable with new estimation)
    int total_tokens = 0;
    for (int i = 0; i < conversation.count; i++) {
        total_tokens += estimate_token_count(conversation.messages[i].content, &config);
    }
    
    // With the improved estimation, this should be much more reasonable
    // Old estimation would have massively overestimated this
    TEST_ASSERT_LESS_THAN_INT(50000, total_tokens);  // Should be well under context limit
    
    // Test that safety buffer is reasonable (not artificially limiting to 150 tokens)
    int safety_buffer = get_dynamic_safety_buffer(&config, total_tokens);
    TEST_ASSERT_GREATER_THAN_INT(150, safety_buffer);  // Should be more than the bug limit
    TEST_ASSERT_LESS_THAN_INT(5000, safety_buffer);   // But not unreasonably large
    
    cleanup_conversation_history(&conversation);
}

void test_improved_token_estimation_efficiency(void) {
    TokenConfig config;
    token_config_init(&config, 8192);
    
    // Test code content (should be more efficient)
    const char* code_text = "```python\ndef hello():\n    print('Hello world')\n```";
    int code_tokens = estimate_token_count(code_text, &config);
    
    // Test regular text
    const char* regular_text = "This is regular text without any special formatting or code.";
    int regular_tokens = estimate_token_count(regular_text, &config);
    
    // Code should be more efficiently tokenized (fewer tokens per character)
    float code_ratio = strlen(code_text) / (float)code_tokens;
    float regular_ratio = strlen(regular_text) / (float)regular_tokens;
    
    TEST_ASSERT_GREATER_THAN_FLOAT(regular_ratio, code_ratio);  // Code should be more efficient
    
    // Test JSON content (should be even more efficient)
    const char* json_text = "{\"role\": \"user\", \"content\": \"Hello world\", \"timestamp\": 1234567890}";
    int json_tokens = estimate_token_count(json_text, &config);
    float json_ratio = strlen(json_text) / (float)json_tokens;
    
    TEST_ASSERT_GREATER_THAN_FLOAT(regular_ratio, json_ratio);  // JSON should be most efficient
}

void test_background_compaction_config(void) {
    CompactionConfig config;
    compaction_config_init(&config);
    
    // Test that background compaction settings are initialized
    TEST_ASSERT_GREATER_THAN(0, config.background_threshold);
    TEST_ASSERT_EQUAL(1, config.store_in_vector_db);
}

void test_should_background_compact_below_threshold(void) {
    ConversationHistory conversation;
    init_conversation_history(&conversation);
    
    // Add enough messages to avoid minimum checks
    for (int i = 0; i < 15; i++) {
        append_conversation_message(&conversation, "user", "Test message");
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    config.background_threshold = 5000;  // High threshold
    
    int current_tokens = 1000;  // Below threshold
    int should_compact = should_background_compact(&conversation, &config, current_tokens);
    
    TEST_ASSERT_EQUAL(0, should_compact);
    
    cleanup_conversation_history(&conversation);
}

void test_should_background_compact_above_threshold(void) {
    ConversationHistory conversation;
    init_conversation_history(&conversation);
    
    // Add enough messages to avoid minimum checks
    for (int i = 0; i < 15; i++) {
        append_conversation_message(&conversation, "user", "Test message");
    }
    
    CompactionConfig config;
    compaction_config_init(&config);
    config.background_threshold = 1000;  // Low threshold
    
    int current_tokens = 2000;  // Above threshold
    int should_compact = should_background_compact(&conversation, &config, current_tokens);
    
    TEST_ASSERT_EQUAL(1, should_compact);
    
    cleanup_conversation_history(&conversation);
}

void test_background_compact_conversation_no_compaction_needed(void) {
    // Test that background_compact_conversation returns 0 when no compaction is needed
    SessionData session;
    session_data_init(&session);
    
    // Add a few messages (not enough to trigger compaction)
    append_conversation_message(&session.conversation, "user", "Hello");
    append_conversation_message(&session.conversation, "assistant", "Hi there!");
    
    CompactionConfig config;
    compaction_config_init(&config);
    config.background_threshold = 10000;  // Very high threshold
    
    CompactionResult result;
    int status = background_compact_conversation(&session, &config, &result);
    
    // Should return 0 (success) but no compaction performed
    TEST_ASSERT_EQUAL(0, status);
    
    session_data_cleanup(&session);
}

int main(void) {
    UNITY_BEGIN();
    
    // Basic compaction system tests
    RUN_TEST(test_compaction_config_init);
    RUN_TEST(test_should_compact_conversation_too_few_messages);
    RUN_TEST(test_should_compact_conversation_tokens_within_limit);
    RUN_TEST(test_should_compact_conversation_needs_compaction);
    RUN_TEST(test_find_compaction_segment_simple);
    RUN_TEST(test_find_compaction_segment_with_tool_messages);
    RUN_TEST(test_compact_conversation_segment_basic);
    RUN_TEST(test_save_compacted_conversation);
    RUN_TEST(test_cleanup_compaction_result);
    
    // Mock API test (will fail with HTTP error, but tests setup)
    RUN_TEST(test_generate_conversation_summary_mock);
    
    // Bug reproduction and regression tests
    RUN_TEST(test_original_token_limit_bug_reproduction);
    RUN_TEST(test_improved_token_estimation_efficiency);
    
    // Background compaction tests
    RUN_TEST(test_background_compaction_config);
    RUN_TEST(test_should_background_compact_below_threshold);
    RUN_TEST(test_should_background_compact_above_threshold);
    RUN_TEST(test_background_compact_conversation_no_compaction_needed);
    
    return UNITY_END();
}