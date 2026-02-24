#include "unity/unity.h"
#include "lib/llm/llm_provider.h"
#include "session/conversation_tracker.h"
#include "ui/output_formatter.h"
#include "network/streaming.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

void test_codex_detect_provider(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);
    TEST_ASSERT_EQUAL_STRING("Codex", provider->capabilities.name);
}

void test_codex_not_detected_for_openai(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://api.openai.com/v1/chat/completions");
    TEST_ASSERT_NOT_NULL(provider);
    /* Should match OpenAI, not Codex */
    TEST_ASSERT_EQUAL_STRING("OpenAI", provider->capabilities.name);
}

void test_codex_account_id(void) {
    codex_set_account_id("acct_test123");
    const char *id = codex_get_account_id();
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_EQUAL_STRING("acct_test123", id);

    codex_set_account_id(NULL);
    TEST_ASSERT_NULL(codex_get_account_id());
}

void test_codex_build_headers(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    codex_set_account_id("acct_xyz");

    const char *headers[8] = {0};
    int count = provider->build_headers(provider, "test_token", headers, 8);

    TEST_ASSERT_GREATER_OR_EQUAL(3, count);

    /* Verify auth header */
    int found_auth = 0, found_account = 0;
    for (int i = 0; i < count; i++) {
        if (strstr(headers[i], "Authorization: Bearer test_token")) found_auth = 1;
        if (strstr(headers[i], "chatgpt-account-id: acct_xyz")) found_account = 1;
    }
    TEST_ASSERT_TRUE(found_auth);
    TEST_ASSERT_TRUE(found_account);

    codex_set_account_id(NULL);
}

void test_codex_parse_response(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    const char *json =
        "{\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\",\"text\":\"Hello!\"}]}],"
        "\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";

    ParsedResponse result = {0};
    int rc = provider->parse_response(provider, json, &result);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Hello!", result.response_content);
    TEST_ASSERT_EQUAL_INT(10, result.prompt_tokens);
    TEST_ASSERT_EQUAL_INT(5, result.completion_tokens);

    cleanup_parsed_response(&result);
}

void test_codex_parse_stream_text_delta(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    StreamingContext *ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    const char *event = "{\"type\":\"response.output_text.delta\",\"delta\":\"Hello world\"}";
    int rc = provider->parse_stream_event(provider, ctx, event, strlen(event));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(ctx->text_content);
    TEST_ASSERT_EQUAL_STRING("Hello world", ctx->text_content);

    streaming_context_free(ctx);
}

void test_codex_parse_stream_completed(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    StreamingContext *ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    const char *event =
        "{\"type\":\"response.completed\","
        "\"response\":{\"usage\":{\"input_tokens\":20,\"output_tokens\":10}}}";
    int rc = provider->parse_stream_event(provider, ctx, event, strlen(event));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_INT(20, ctx->input_tokens);
    TEST_ASSERT_EQUAL_INT(10, ctx->output_tokens);

    streaming_context_free(ctx);
}

void test_codex_parse_stream_tool_call(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    StreamingContext *ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    /* Tool call start with name and call_id */
    const char *start_event =
        "{\"type\":\"response.function_call_arguments.delta\","
        "\"call_id\":\"call_abc123\",\"name\":\"read_file\",\"delta\":\"{\\\"path\\\": \\\"\"}";
    int rc = provider->parse_stream_event(provider, ctx, start_event, strlen(start_event));
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Subsequent delta */
    const char *delta_event =
        "{\"type\":\"response.function_call_arguments.delta\","
        "\"call_id\":\"call_abc123\",\"delta\":\"test.c\\\"}\"}";
    rc = provider->parse_stream_event(provider, ctx, delta_event, strlen(delta_event));
    TEST_ASSERT_EQUAL_INT(0, rc);

    streaming_context_free(ctx);
}

void test_codex_build_request_with_tool_result(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    append_conversation_message(&history, "user", "Read this file");
    append_tool_message(&history, "file contents here", "call_abc123", "read_file");

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "Now summarize", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Verify tool result is formatted as function_call_output */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"function_call_output\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"call_id\":\"call_abc123\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"output\":\"file contents here\""));

    free(json);
    cleanup_conversation_history(&history);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_codex_detect_provider);
    RUN_TEST(test_codex_not_detected_for_openai);
    RUN_TEST(test_codex_account_id);
    RUN_TEST(test_codex_build_headers);
    RUN_TEST(test_codex_parse_response);
    RUN_TEST(test_codex_parse_stream_text_delta);
    RUN_TEST(test_codex_parse_stream_completed);
    RUN_TEST(test_codex_parse_stream_tool_call);
    RUN_TEST(test_codex_build_request_with_tool_result);
    return UNITY_END();
}
