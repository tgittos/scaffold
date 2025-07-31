#include "unity.h"
#include "token_manager.h"
#include "ralph.h"
#include <string.h>

void setUp(void) {
    // This is run before each test
}

void tearDown(void) {
    // This is run after each test
}

void test_token_config_init_with_valid_values(void) {
    TokenConfig config;
    token_config_init(&config, 16384, 32768);
    
    TEST_ASSERT_EQUAL_INT(16384, config.context_window);
    TEST_ASSERT_EQUAL_INT(32768, config.max_context_window);
    TEST_ASSERT_EQUAL_INT(150, config.min_response_tokens);
    TEST_ASSERT_EQUAL_INT(50, config.safety_buffer_base);
    TEST_ASSERT_EQUAL_FLOAT(0.05f, config.safety_buffer_ratio);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, config.chars_per_token);
}

void test_token_config_init_with_zero_values(void) {
    TokenConfig config;
    token_config_init(&config, 0, 0);
    
    TEST_ASSERT_EQUAL_INT(8192, config.context_window);  // Should use default
    TEST_ASSERT_EQUAL_INT(8192, config.max_context_window);  // Should match context_window
}

void test_token_config_init_max_smaller_than_context(void) {
    TokenConfig config;
    token_config_init(&config, 16384, 8192);  // max < context
    
    TEST_ASSERT_EQUAL_INT(16384, config.context_window);
    TEST_ASSERT_EQUAL_INT(16384, config.max_context_window);  // Should be adjusted
}

void test_estimate_token_count_simple_text(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    const char* text = "Hello world";
    int tokens = estimate_token_count(text, &config);
    
    // "Hello world" = 11 chars / 3.5 = ~3.14 -> 4 tokens
    TEST_ASSERT_EQUAL_INT(4, tokens);
}

void test_estimate_token_count_with_tools(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    const char* text = "This message contains \"tools\" in it";
    int tokens = estimate_token_count(text, &config);
    
    // Should include tool overhead (+100)
    int base_tokens = (int)ceil(strlen(text) / 3.5f);
    TEST_ASSERT_EQUAL_INT(base_tokens + 100, tokens);
}

void test_estimate_token_count_with_system(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    const char* text = "This is a \"system\" message";
    int tokens = estimate_token_count(text, &config);
    
    // Should include system overhead (+20)
    int base_tokens = (int)ceil(strlen(text) / 3.5f);
    TEST_ASSERT_EQUAL_INT(base_tokens + 20, tokens);
}

void test_get_dynamic_safety_buffer_normal(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    int buffer = get_dynamic_safety_buffer(&config, 1000);
    
    // Base (50) + ratio (8192 * 0.05 = 409) = 459
    int expected = 50 + (int)(8192 * 0.05f);
    TEST_ASSERT_EQUAL_INT(expected, buffer);
}

void test_get_dynamic_safety_buffer_complex_prompt(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    // Complex prompt (>70% of context window)
    int complex_tokens = (int)(8192 * 0.8);
    int buffer = get_dynamic_safety_buffer(&config, complex_tokens);
    
    // Base + ratio + complex penalty (+50)
    int expected = 50 + (int)(8192 * 0.05f) + 50;
    TEST_ASSERT_EQUAL_INT(expected, buffer);
}

void test_validate_token_config_valid(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    int result = validate_token_config(&config);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_validate_token_config_invalid_context_window(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    config.context_window = 0;
    
    int result = validate_token_config(&config);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_validate_token_config_invalid_min_response_tokens(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    config.min_response_tokens = 10000;  // Larger than context window
    
    int result = validate_token_config(&config);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_validate_token_config_invalid_chars_per_token(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    config.chars_per_token = 0.0f;
    
    int result = validate_token_config(&config);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_trim_conversation_empty_history(void) {
    TokenConfig config;
    token_config_init(&config, 8192, 8192);
    
    ConversationHistory conversation = {0};
    int trimmed = trim_conversation_for_tokens(&conversation, &config, 1000, NULL);
    
    TEST_ASSERT_EQUAL_INT(0, trimmed);
    TEST_ASSERT_EQUAL_INT(0, conversation.count);
}

void test_calculate_token_allocation_simple(void) {
    // Create a minimal session
    RalphSession session = {0};
    session.config.context_window = 8192;
    session.config.max_context_window = 8192;
    session.config.system_prompt = strdup("You are a helpful assistant.");
    session.conversation.count = 0;
    session.tools.function_count = 0;
    
    TokenConfig config;
    token_config_init(&config, session.config.context_window, session.config.max_context_window);
    TokenUsage usage;
    
    int result = calculate_token_allocation(&session, "Hello", &config, &usage);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_GREATER_THAN_INT(0, usage.available_response_tokens);
    TEST_ASSERT_GREATER_THAN_INT(0, usage.total_prompt_tokens);
    TEST_ASSERT_EQUAL_INT(8192, usage.context_window_used);
    
    // Cleanup
    free(session.config.system_prompt);
}

void test_calculate_token_allocation_with_max_context_window(void) {
    // Create a session with max context window larger than context window
    RalphSession session = {0};
    session.config.context_window = 8192;
    session.config.max_context_window = 16384;
    session.config.system_prompt = strdup("You are a helpful assistant.");
    session.conversation.count = 0;
    session.tools.function_count = 0;
    
    TokenConfig config;
    token_config_init(&config, session.config.context_window, session.config.max_context_window);
    TokenUsage usage;
    
    int result = calculate_token_allocation(&session, "Hello", &config, &usage);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(16384, usage.context_window_used);  // Should use max context window
    TEST_ASSERT_GREATER_THAN_INT(0, usage.available_response_tokens);
    
    // Cleanup
    free(session.config.system_prompt);
}

void test_calculate_token_allocation_insufficient_tokens(void) {
    // Create a session with very small context window
    RalphSession session = {0};
    session.config.context_window = 200;  // Very small
    session.config.max_context_window = 200;
    session.config.system_prompt = strdup("You are a helpful assistant with a very long system prompt that takes up most of the context window space and leaves little room for the actual response which should trigger the minimum response token logic.");
    session.conversation.count = 0;
    session.tools.function_count = 0;
    
    TokenConfig config;
    token_config_init(&config, session.config.context_window, session.config.max_context_window);
    TokenUsage usage;
    
    int result = calculate_token_allocation(&session, "Hello", &config, &usage);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(150, usage.available_response_tokens);  // Should use minimum
    
    // Cleanup
    free(session.config.system_prompt);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_token_config_init_with_valid_values);
    RUN_TEST(test_token_config_init_with_zero_values);
    RUN_TEST(test_token_config_init_max_smaller_than_context);
    RUN_TEST(test_estimate_token_count_simple_text);
    RUN_TEST(test_estimate_token_count_with_tools);
    RUN_TEST(test_estimate_token_count_with_system);
    RUN_TEST(test_get_dynamic_safety_buffer_normal);
    RUN_TEST(test_get_dynamic_safety_buffer_complex_prompt);
    RUN_TEST(test_validate_token_config_valid);
    RUN_TEST(test_validate_token_config_invalid_context_window);
    RUN_TEST(test_validate_token_config_invalid_min_response_tokens);
    RUN_TEST(test_validate_token_config_invalid_chars_per_token);
    RUN_TEST(test_trim_conversation_empty_history);
    RUN_TEST(test_calculate_token_allocation_simple);
    RUN_TEST(test_calculate_token_allocation_with_max_context_window);
    RUN_TEST(test_calculate_token_allocation_insufficient_tokens);
    
    return UNITY_END();
}