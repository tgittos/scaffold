#include "unity/unity.h"
#include "session/conversation_tracker.h"
#include "lib/llm/llm_provider.h"
#include "network/streaming.h"
#include <stdlib.h>
#include <string.h>

// Test globals
static StreamingContext* test_ctx = NULL;
static LLMProvider* anthropic_provider = NULL;

// External provider access
extern int register_anthropic_provider(ProviderRegistry* registry);

// Get the Anthropic provider for testing
static LLMProvider* get_anthropic_provider(void) {
    static ProviderRegistry registry;
    static int initialized = 0;
    if (!initialized) {
        init_provider_registry(&registry);
        register_anthropic_provider(&registry);
        initialized = 1;
    }
    return detect_provider_for_url(&registry, "https://api.anthropic.com/v1/messages");
}

void setUp(void) {
    test_ctx = streaming_context_create();
    anthropic_provider = get_anthropic_provider();
}

void tearDown(void) {
    streaming_context_free(test_ctx);
    test_ctx = NULL;
}

// =============================================================================
// Provider Detection Tests
// =============================================================================

void test_anthropic_supports_streaming(void) {
    TEST_ASSERT_NOT_NULL(anthropic_provider);
    TEST_ASSERT_NOT_NULL(anthropic_provider->supports_streaming);
    TEST_ASSERT_EQUAL_INT(1, anthropic_provider->supports_streaming(anthropic_provider));
}

void test_anthropic_has_stream_event_parser(void) {
    TEST_ASSERT_NOT_NULL(anthropic_provider);
    TEST_ASSERT_NOT_NULL(anthropic_provider->parse_stream_event);
}

void test_anthropic_has_streaming_request_builder(void) {
    TEST_ASSERT_NOT_NULL(anthropic_provider);
    TEST_ASSERT_NOT_NULL(anthropic_provider->build_streaming_request_json);
}

// =============================================================================
// Message Start Parsing Tests
// =============================================================================

void test_parse_message_start_input_tokens(void) {
    const char* json =
        "{\"type\":\"message_start\",\"message\":{\"id\":\"msg_123\","
        "\"type\":\"message\",\"role\":\"assistant\",\"content\":[],"
        "\"model\":\"claude-3-opus-20240229\",\"usage\":{\"input_tokens\":25}}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(25, test_ctx->input_tokens);
}

// =============================================================================
// Content Block Start Parsing Tests
// =============================================================================

void test_parse_content_block_start_text(void) {
    const char* json =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    // Text block start doesn't emit content yet
    TEST_ASSERT_EQUAL_STRING("", test_ctx->text_content);
}

void test_parse_content_block_start_thinking(void) {
    const char* json =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    // Thinking block start doesn't emit content yet
    TEST_ASSERT_EQUAL_STRING("", test_ctx->thinking_content);
}

void test_parse_content_block_start_tool_use(void) {
    const char* json =
        "{\"type\":\"content_block_start\",\"index\":1,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_123\",\"name\":\"get_weather\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(1, test_ctx->tool_uses.count);
    TEST_ASSERT_EQUAL_STRING("toolu_123", test_ctx->tool_uses.data[0].id);
    TEST_ASSERT_EQUAL_STRING("get_weather", test_ctx->tool_uses.data[0].name);
}

// =============================================================================
// Content Block Delta Parsing Tests
// =============================================================================

void test_parse_text_delta(void) {
    // First set up a text block
    const char* block_start =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"text\",\"text\":\"\"}}";
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, block_start, strlen(block_start));

    // Then parse a text delta
    const char* json =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("Hello", test_ctx->text_content);
}

void test_parse_multiple_text_deltas(void) {
    const char* json1 =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello\"}}";
    const char* json2 =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\" world\"}}";

    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json1, strlen(json1));
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json2, strlen(json2));

    TEST_ASSERT_EQUAL_STRING("Hello world", test_ctx->text_content);
}

void test_parse_thinking_delta(void) {
    // First set up a thinking block
    const char* block_start =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"thinking\",\"thinking\":\"\"}}";
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, block_start, strlen(block_start));

    // Then parse a thinking delta
    const char* json =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"Let me think...\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("Let me think...", test_ctx->thinking_content);
}

void test_parse_tool_input_json_delta(void) {
    // First set up a tool_use block
    const char* block_start =
        "{\"type\":\"content_block_start\",\"index\":1,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"toolu_xyz\",\"name\":\"shell_execute\"}}";
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, block_start, strlen(block_start));

    // Then parse tool input deltas
    const char* json1 =
        "{\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"cmd\\\":\"}}";
    const char* json2 =
        "{\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"\\\"ls\\\"}\"}}";

    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json1, strlen(json1));
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json2, strlen(json2));

    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"ls\"}", test_ctx->tool_uses.data[0].arguments_json);
}

// =============================================================================
// Message Delta Parsing Tests
// =============================================================================

void test_parse_message_delta_stop_reason(void) {
    const char* json =
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
        "\"usage\":{\"output_tokens\":12}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NOT_NULL(test_ctx->stop_reason);
    TEST_ASSERT_EQUAL_STRING("end_turn", test_ctx->stop_reason);
    TEST_ASSERT_EQUAL_INT(12, test_ctx->output_tokens);
}

void test_parse_message_delta_tool_use_stop(void) {
    const char* json =
        "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"},"
        "\"usage\":{\"output_tokens\":50}}";

    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_STRING("tool_use", test_ctx->stop_reason);
    TEST_ASSERT_EQUAL_INT(50, test_ctx->output_tokens);
}

// =============================================================================
// Message Stop Parsing Tests
// =============================================================================

void test_parse_message_stop(void) {
    // Set up a stop reason first
    const char* delta = "{\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},\"usage\":{\"output_tokens\":10}}";
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, delta, strlen(delta));

    // Then parse message_stop
    const char* json = "{\"type\":\"message_stop\"}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(STREAM_STATE_COMPLETE, test_ctx->state);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

void test_parse_error_event(void) {
    const char* json =
        "{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\","
        "\"message\":\"Overloaded\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(STREAM_STATE_ERROR, test_ctx->state);
    TEST_ASSERT_NOT_NULL(test_ctx->error_message);
    TEST_ASSERT_EQUAL_STRING("Overloaded", test_ctx->error_message);
}

void test_parse_ping_event(void) {
    const char* json = "{\"type\":\"ping\"}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    // Ping should be silently ignored
    TEST_ASSERT_EQUAL_STRING("", test_ctx->text_content);
}

void test_parse_invalid_json(void) {
    const char* invalid = "not valid json {{{";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, invalid, strlen(invalid));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_null_context(void) {
    const char* json = "{\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"test\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, NULL, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_null_data(void) {
    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, NULL, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_zero_length_data(void) {
    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, "test", 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_missing_type_field(void) {
    const char* json = "{\"delta\":{\"text\":\"hello\"}}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// =============================================================================
// Streaming Request Builder Tests
// =============================================================================

void test_build_streaming_request_includes_stream_true(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    SystemPromptParts sys = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char* json = anthropic_provider->build_streaming_request_json(
        anthropic_provider, "claude-3-opus-20240229", &sys, &history, "Hello", 1000, NULL);

    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"stream\":true"));

    free(json);
    cleanup_conversation_history(&history);
}

void test_build_streaming_request_null_provider(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    SystemPromptParts sys = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char* json = anthropic_provider->build_streaming_request_json(
        NULL, "claude-3-opus-20240229", &sys, &history, "Hello", 1000, NULL);

    TEST_ASSERT_NULL(json);

    cleanup_conversation_history(&history);
}

void test_build_streaming_request_null_model(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    SystemPromptParts sys = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char* json = anthropic_provider->build_streaming_request_json(
        anthropic_provider, NULL, &sys, &history, "Hello", 1000, NULL);

    TEST_ASSERT_NULL(json);

    cleanup_conversation_history(&history);
}

void test_build_streaming_request_null_conversation(void) {
    SystemPromptParts sys = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char* json = anthropic_provider->build_streaming_request_json(
        anthropic_provider, "claude-3-opus-20240229", &sys, NULL, "Hello", 1000, NULL);

    TEST_ASSERT_NULL(json);
}

// =============================================================================
// Content Block Stop Tests
// =============================================================================

void test_parse_content_block_stop(void) {
    const char* json = "{\"type\":\"content_block_stop\",\"index\":0}";

    int result = anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    // Block stop should succeed without error
}

// =============================================================================
// Multiple Tool Calls Tests
// =============================================================================

void test_parse_multiple_tool_calls(void) {
    // First tool
    const char* tool1_start =
        "{\"type\":\"content_block_start\",\"index\":0,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"tool_1\",\"name\":\"tool_a\"}}";
    const char* tool1_delta =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"a\\\":1}\"}}";
    const char* tool1_stop = "{\"type\":\"content_block_stop\",\"index\":0}";

    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, tool1_start, strlen(tool1_start));
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, tool1_delta, strlen(tool1_delta));
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, tool1_stop, strlen(tool1_stop));

    // Second tool
    const char* tool2_start =
        "{\"type\":\"content_block_start\",\"index\":1,"
        "\"content_block\":{\"type\":\"tool_use\",\"id\":\"tool_2\",\"name\":\"tool_b\"}}";
    const char* tool2_delta =
        "{\"type\":\"content_block_delta\",\"index\":1,"
        "\"delta\":{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"b\\\":2}\"}}";

    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, tool2_start, strlen(tool2_start));
    anthropic_provider->parse_stream_event(anthropic_provider, test_ctx, tool2_delta, strlen(tool2_delta));

    TEST_ASSERT_EQUAL_INT(2, test_ctx->tool_uses.count);
    TEST_ASSERT_EQUAL_STRING("tool_1", test_ctx->tool_uses.data[0].id);
    TEST_ASSERT_EQUAL_STRING("tool_a", test_ctx->tool_uses.data[0].name);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", test_ctx->tool_uses.data[0].arguments_json);
    TEST_ASSERT_EQUAL_STRING("tool_2", test_ctx->tool_uses.data[1].id);
    TEST_ASSERT_EQUAL_STRING("tool_b", test_ctx->tool_uses.data[1].name);
    TEST_ASSERT_EQUAL_STRING("{\"b\":2}", test_ctx->tool_uses.data[1].arguments_json);
}

// =============================================================================
// Main
// =============================================================================

int main(void) {
    UNITY_BEGIN();

    // Provider detection tests
    RUN_TEST(test_anthropic_supports_streaming);
    RUN_TEST(test_anthropic_has_stream_event_parser);
    RUN_TEST(test_anthropic_has_streaming_request_builder);

    // Message start parsing tests
    RUN_TEST(test_parse_message_start_input_tokens);

    // Content block start parsing tests
    RUN_TEST(test_parse_content_block_start_text);
    RUN_TEST(test_parse_content_block_start_thinking);
    RUN_TEST(test_parse_content_block_start_tool_use);

    // Content block delta parsing tests
    RUN_TEST(test_parse_text_delta);
    RUN_TEST(test_parse_multiple_text_deltas);
    RUN_TEST(test_parse_thinking_delta);
    RUN_TEST(test_parse_tool_input_json_delta);

    // Message delta parsing tests
    RUN_TEST(test_parse_message_delta_stop_reason);
    RUN_TEST(test_parse_message_delta_tool_use_stop);

    // Message stop parsing tests
    RUN_TEST(test_parse_message_stop);

    // Content block stop tests
    RUN_TEST(test_parse_content_block_stop);

    // Multiple tool calls tests
    RUN_TEST(test_parse_multiple_tool_calls);

    // Error handling tests
    RUN_TEST(test_parse_error_event);
    RUN_TEST(test_parse_ping_event);
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_parse_null_context);
    RUN_TEST(test_parse_null_data);
    RUN_TEST(test_parse_zero_length_data);
    RUN_TEST(test_parse_missing_type_field);

    // Streaming request builder tests
    RUN_TEST(test_build_streaming_request_includes_stream_true);
    RUN_TEST(test_build_streaming_request_null_provider);
    RUN_TEST(test_build_streaming_request_null_model);
    RUN_TEST(test_build_streaming_request_null_conversation);

    return UNITY_END();
}
