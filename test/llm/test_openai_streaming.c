#include "unity/unity.h"
#include "lib/llm/llm_provider.h"
#include "network/streaming.h"
#include <stdlib.h>
#include <string.h>

// Test globals
static StreamingContext* test_ctx = NULL;
static LLMProvider* openai_provider = NULL;

// External provider access
extern int register_openai_provider(ProviderRegistry* registry);

// Get the OpenAI provider for testing
static LLMProvider* get_openai_provider(void) {
    static ProviderRegistry registry;
    static int initialized = 0;
    if (!initialized) {
        init_provider_registry(&registry);
        register_openai_provider(&registry);
        initialized = 1;
    }
    return detect_provider_for_url(&registry, "https://api.openai.com/v1/chat/completions");
}

void setUp(void) {
    test_ctx = streaming_context_create();
    openai_provider = get_openai_provider();
}

void tearDown(void) {
    streaming_context_free(test_ctx);
    test_ctx = NULL;
}

// =============================================================================
// Provider Detection Tests
// =============================================================================

void test_openai_supports_streaming(void) {
    TEST_ASSERT_NOT_NULL(openai_provider);
    TEST_ASSERT_NOT_NULL(openai_provider->supports_streaming);
    TEST_ASSERT_EQUAL_INT(1, openai_provider->supports_streaming(openai_provider));
}

void test_openai_has_stream_event_parser(void) {
    TEST_ASSERT_NOT_NULL(openai_provider);
    TEST_ASSERT_NOT_NULL(openai_provider->parse_stream_event);
}

void test_openai_has_streaming_request_builder(void) {
    TEST_ASSERT_NOT_NULL(openai_provider);
    TEST_ASSERT_NOT_NULL(openai_provider->build_streaming_request_json);
}

// =============================================================================
// Text Content Parsing Tests
// =============================================================================

void test_parse_text_content_delta(void) {
    const char* json = "{\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("Hello", test_ctx->text_content);
}

void test_parse_multiple_text_deltas(void) {
    const char* json1 = "{\"choices\":[{\"delta\":{\"content\":\"Hello\"}}]}";
    const char* json2 = "{\"choices\":[{\"delta\":{\"content\":\" World\"}}]}";

    openai_provider->parse_stream_event(openai_provider, test_ctx, json1, strlen(json1));
    openai_provider->parse_stream_event(openai_provider, test_ctx, json2, strlen(json2));

    TEST_ASSERT_EQUAL_STRING("Hello World", test_ctx->text_content);
}

void test_parse_empty_content_delta(void) {
    const char* json = "{\"choices\":[{\"delta\":{\"content\":\"\"}}]}";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("", test_ctx->text_content);
}

void test_parse_null_content_delta(void) {
    const char* json = "{\"choices\":[{\"delta\":{}}]}";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_STRING("", test_ctx->text_content);
}

// =============================================================================
// Tool Call Parsing Tests
// =============================================================================

void test_parse_tool_call_start(void) {
    const char* json =
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":0,"
        "\"id\":\"call_abc123\","
        "\"type\":\"function\","
        "\"function\":{\"name\":\"get_weather\",\"arguments\":\"\"}"
        "}]}}]}";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(1, test_ctx->tool_uses.count);
    TEST_ASSERT_EQUAL_STRING("call_abc123", test_ctx->tool_uses.data[0].id);
    TEST_ASSERT_EQUAL_STRING("get_weather", test_ctx->tool_uses.data[0].name);
}

void test_parse_tool_call_arguments_delta(void) {
    // First send the tool start
    const char* json_start =
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":0,"
        "\"id\":\"call_xyz789\","
        "\"type\":\"function\","
        "\"function\":{\"name\":\"shell_execute\",\"arguments\":\"\"}"
        "}]}}]}";

    openai_provider->parse_stream_event(openai_provider, test_ctx, json_start, strlen(json_start));

    // Then send argument deltas
    const char* json_delta1 =
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":0,"
        "\"function\":{\"arguments\":\"{\\\"cmd\\\":\"}"
        "}]}}]}";

    const char* json_delta2 =
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":0,"
        "\"function\":{\"arguments\":\"\\\"ls\\\"}\"}"
        "}]}}]}";

    openai_provider->parse_stream_event(openai_provider, test_ctx, json_delta1, strlen(json_delta1));
    openai_provider->parse_stream_event(openai_provider, test_ctx, json_delta2, strlen(json_delta2));

    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"ls\"}", test_ctx->tool_uses.data[0].arguments_json);
}

void test_parse_multiple_tool_calls(void) {
    const char* json1 =
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":0,"
        "\"id\":\"call_1\","
        "\"function\":{\"name\":\"tool_a\",\"arguments\":\"\"}"
        "}]}}]}";

    const char* json2 =
        "{\"choices\":[{\"delta\":{\"tool_calls\":[{"
        "\"index\":1,"
        "\"id\":\"call_2\","
        "\"function\":{\"name\":\"tool_b\",\"arguments\":\"\"}"
        "}]}}]}";

    openai_provider->parse_stream_event(openai_provider, test_ctx, json1, strlen(json1));
    openai_provider->parse_stream_event(openai_provider, test_ctx, json2, strlen(json2));

    TEST_ASSERT_EQUAL_INT(2, test_ctx->tool_uses.count);
    TEST_ASSERT_EQUAL_STRING("call_1", test_ctx->tool_uses.data[0].id);
    TEST_ASSERT_EQUAL_STRING("call_2", test_ctx->tool_uses.data[1].id);
}

// =============================================================================
// Finish Reason Parsing Tests
// =============================================================================

void test_parse_finish_reason_stop(void) {
    const char* json = "{\"choices\":[{\"finish_reason\":\"stop\"}]}";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NOT_NULL(test_ctx->stop_reason);
    TEST_ASSERT_EQUAL_STRING("stop", test_ctx->stop_reason);
}

void test_parse_finish_reason_tool_calls(void) {
    const char* json = "{\"choices\":[{\"finish_reason\":\"tool_calls\"}]}";

    openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));
    TEST_ASSERT_EQUAL_STRING("tool_calls", test_ctx->stop_reason);
}

// =============================================================================
// Usage Statistics Parsing Tests
// =============================================================================

void test_parse_usage_statistics(void) {
    const char* json =
        "{\"choices\":[{\"delta\":{}}],"
        "\"usage\":{\"prompt_tokens\":100,\"completion_tokens\":50}}";

    openai_provider->parse_stream_event(openai_provider, test_ctx, json, strlen(json));

    TEST_ASSERT_EQUAL_INT(100, test_ctx->input_tokens);
    TEST_ASSERT_EQUAL_INT(50, test_ctx->output_tokens);
}

// =============================================================================
// Done Signal Handling Tests
// =============================================================================

void test_parse_done_signal(void) {
    const char* done = "[DONE]";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, done, strlen(done));
    TEST_ASSERT_EQUAL_INT(0, result);
}

// =============================================================================
// Error Handling Tests
// =============================================================================

void test_parse_invalid_json(void) {
    const char* invalid = "not valid json {{{";

    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, invalid, strlen(invalid));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_null_context(void) {
    const char* json = "{\"choices\":[{\"delta\":{\"content\":\"test\"}}]}";

    int result = openai_provider->parse_stream_event(openai_provider, NULL, json, strlen(json));
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_null_data(void) {
    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, NULL, 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_zero_length_data(void) {
    int result = openai_provider->parse_stream_event(openai_provider, test_ctx, "test", 0);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

// =============================================================================
// Streaming Request Builder Tests
// =============================================================================

void test_build_streaming_request_includes_stream_true(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    char* json = openai_provider->build_streaming_request_json(
        openai_provider, "gpt-4", "You are helpful.", &history, "Hello", 1000, NULL);

    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"stream\":true"));

    free(json);
    cleanup_conversation_history(&history);
}

void test_build_streaming_request_includes_stream_options(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    char* json = openai_provider->build_streaming_request_json(
        openai_provider, "gpt-4", "You are helpful.", &history, "Hello", 1000, NULL);

    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_NOT_NULL(strstr(json, "\"stream_options\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"include_usage\":true"));

    free(json);
    cleanup_conversation_history(&history);
}

void test_build_streaming_request_null_provider(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    char* json = openai_provider->build_streaming_request_json(
        NULL, "gpt-4", "You are helpful.", &history, "Hello", 1000, NULL);

    TEST_ASSERT_NULL(json);

    cleanup_conversation_history(&history);
}

void test_build_streaming_request_null_model(void) {
    ConversationHistory history = {0};
    init_conversation_history(&history);

    char* json = openai_provider->build_streaming_request_json(
        openai_provider, NULL, "You are helpful.", &history, "Hello", 1000, NULL);

    TEST_ASSERT_NULL(json);

    cleanup_conversation_history(&history);
}

// =============================================================================
// Main
// =============================================================================

int main(void) {
    UNITY_BEGIN();

    // Provider detection tests
    RUN_TEST(test_openai_supports_streaming);
    RUN_TEST(test_openai_has_stream_event_parser);
    RUN_TEST(test_openai_has_streaming_request_builder);

    // Text content parsing tests
    RUN_TEST(test_parse_text_content_delta);
    RUN_TEST(test_parse_multiple_text_deltas);
    RUN_TEST(test_parse_empty_content_delta);
    RUN_TEST(test_parse_null_content_delta);

    // Tool call parsing tests
    RUN_TEST(test_parse_tool_call_start);
    RUN_TEST(test_parse_tool_call_arguments_delta);
    RUN_TEST(test_parse_multiple_tool_calls);

    // Finish reason parsing tests
    RUN_TEST(test_parse_finish_reason_stop);
    RUN_TEST(test_parse_finish_reason_tool_calls);

    // Usage statistics parsing tests
    RUN_TEST(test_parse_usage_statistics);

    // Done signal handling tests
    RUN_TEST(test_parse_done_signal);

    // Error handling tests
    RUN_TEST(test_parse_invalid_json);
    RUN_TEST(test_parse_null_context);
    RUN_TEST(test_parse_null_data);
    RUN_TEST(test_parse_zero_length_data);

    // Streaming request builder tests
    RUN_TEST(test_build_streaming_request_includes_stream_true);
    RUN_TEST(test_build_streaming_request_includes_stream_options);
    RUN_TEST(test_build_streaming_request_null_provider);
    RUN_TEST(test_build_streaming_request_null_model);

    return UNITY_END();
}
