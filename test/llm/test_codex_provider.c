#include "unity/unity.h"
#include "lib/llm/llm_provider.h"
#include "llm/providers/codex_provider.h"
#include "session/conversation_tracker.h"
#include "ui/output_formatter.h"
#include "network/streaming.h"
#include <cJSON.h>
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

    TEST_ASSERT_GREATER_OR_EQUAL(2, count);

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

void test_codex_parse_response_error(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    const char *json =
        "{\"error\":{\"message\":\"Rate limit exceeded\",\"type\":\"rate_limit_error\"}}";

    ParsedResponse result = {0};
    int rc = provider->parse_response(provider, json, &result);
    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_NOT_NULL(result.response_content);
    TEST_ASSERT_EQUAL_STRING("Rate limit exceeded", result.response_content);

    cleanup_parsed_response(&result);
}

void test_codex_parse_response_multi_output_text(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    const char *json =
        "{\"output\":[{\"type\":\"message\",\"content\":["
        "{\"type\":\"output_text\",\"text\":\"First\"},"
        "{\"type\":\"output_text\",\"text\":\"Last\"}"
        "]}],\"usage\":{\"input_tokens\":5,\"output_tokens\":2}}";

    ParsedResponse result = {0};
    int rc = provider->parse_response(provider, json, &result);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("Last", result.response_content);

    cleanup_parsed_response(&result);
}

void test_codex_build_request_with_tools(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "hello", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Verify flat format: no nested "function" wrapper */
    TEST_ASSERT_NULL(strstr(json, "\"function\":{"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"instructions\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"model\":\"codex-mini\""));

    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_build_streaming_request(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_streaming_request_json(provider, "codex-mini", &prompt,
                                                          &history, "hello", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_NOT_NULL(strstr(json, "\"stream\":true"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"store\":false"));

    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_parse_stream_done_sentinel(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    StreamingContext *ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    int rc = provider->parse_stream_event(provider, ctx, "[DONE]", 6);
    TEST_ASSERT_EQUAL_INT(0, rc);

    streaming_context_free(ctx);
}

void test_codex_parse_stream_null_empty(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    StreamingContext *ctx = streaming_context_create();
    TEST_ASSERT_NOT_NULL(ctx);

    TEST_ASSERT_EQUAL_INT(-1, provider->parse_stream_event(provider, NULL, "data", 4));
    TEST_ASSERT_EQUAL_INT(-1, provider->parse_stream_event(provider, ctx, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, provider->parse_stream_event(provider, ctx, "data", 0));

    streaming_context_free(ctx);
}

void test_codex_parse_stream_error_events(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    /* Test response.failed */
    StreamingContext *ctx1 = streaming_context_create();
    const char *failed = "{\"type\":\"response.failed\","
                         "\"response\":{\"status_details\":{\"reason\":\"server_error\"}}}";
    int rc = provider->parse_stream_event(provider, ctx1, failed, strlen(failed));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(ctx1->error_message);
    TEST_ASSERT_EQUAL_STRING("server_error", ctx1->error_message);
    streaming_context_free(ctx1);

    /* Test response.incomplete */
    StreamingContext *ctx2 = streaming_context_create();
    const char *incomplete = "{\"type\":\"response.incomplete\","
                             "\"response\":{\"incomplete_details\":{\"reason\":\"max_tokens\"}}}";
    rc = provider->parse_stream_event(provider, ctx2, incomplete, strlen(incomplete));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(ctx2->error_message);
    TEST_ASSERT_EQUAL_STRING("max_tokens", ctx2->error_message);
    streaming_context_free(ctx2);

    /* Test error event */
    StreamingContext *ctx3 = streaming_context_create();
    const char *error = "{\"type\":\"error\",\"error\":{\"message\":\"bad request\"}}";
    rc = provider->parse_stream_event(provider, ctx3, error, strlen(error));
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(ctx3->error_message);
    TEST_ASSERT_EQUAL_STRING("bad request", ctx3->error_message);
    streaming_context_free(ctx3);
}

void test_codex_build_request_with_tool_calls(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    /* Production format from gpt_format_assistant_tool_message() */
    const char *assistant_with_tools =
        "{\"role\": \"assistant\", \"content\": null, "
        "\"tool_calls\": [{\"id\": \"call_1\", \"type\": \"function\", "
        "\"function\": {\"name\": \"read_file\", "
        "\"arguments\": \"{\\\"path\\\":\\\"test.c\\\"}\"}}]}";
    append_conversation_message(&history, "assistant", assistant_with_tools);

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "summarize", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Should emit a function_call item, not summarized text */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"function_call\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"call_id\":\"call_1\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"name\":\"read_file\""));
    /* Should NOT contain summarized text */
    TEST_ASSERT_NULL(strstr(json, "Calling read_file"));

    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_build_request_tool_calls_with_content(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    /* Production format with text content AND tool_calls */
    const char *assistant_with_both =
        "{\"role\": \"assistant\", \"content\": \"Let me check that file.\", "
        "\"tool_calls\": [{\"id\": \"call_2\", \"type\": \"function\", "
        "\"function\": {\"name\": \"read_file\", "
        "\"arguments\": \"{\\\"path\\\":\\\"main.c\\\"}\"}}]}";
    append_conversation_message(&history, "assistant", assistant_with_both);

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "ok", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Should emit both an assistant text item and a function_call item */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"role\":\"assistant\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "Let me check that file."));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"function_call\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"call_id\":\"call_2\""));

    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_build_request_tool_calls_null_content(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    /* Production format with explicit null content */
    const char *assistant_null_content =
        "{\"role\": \"assistant\", \"content\": null, "
        "\"tool_calls\": [{\"id\": \"call_3\", \"type\": \"function\", "
        "\"function\": {\"name\": \"write_file\", "
        "\"arguments\": \"{\\\"path\\\":\\\"out.txt\\\"}\"}}]}";
    append_conversation_message(&history, "assistant", assistant_null_content);

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "ok", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Should emit function_call but NO assistant text item */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"function_call\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"call_id\":\"call_3\""));

    /* Parse and check: no assistant role item should exist */
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    cJSON *input = cJSON_GetObjectItem(root, "input");
    TEST_ASSERT_NOT_NULL(input);
    int found_assistant_text = 0;
    int arr_size = cJSON_GetArraySize(input);
    for (int i = 0; i < arr_size; i++) {
        cJSON *item = cJSON_GetArrayItem(input, i);
        cJSON *role = cJSON_GetObjectItem(item, "role");
        if (role && cJSON_IsString(role) && strcmp(role->valuestring, "assistant") == 0) {
            found_assistant_text = 1;
        }
    }
    TEST_ASSERT_FALSE(found_assistant_text);
    cJSON_Delete(root);

    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_build_request_multiple_tool_calls(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    /* Production format with two tool calls */
    const char *assistant_multi =
        "{\"role\": \"assistant\", \"content\": null, \"tool_calls\": ["
        "{\"id\": \"call_a\", \"type\": \"function\", "
        "\"function\": {\"name\": \"read_file\", \"arguments\": \"{\\\"path\\\":\\\"a.c\\\"}\"}},"
        "{\"id\": \"call_b\", \"type\": \"function\", "
        "\"function\": {\"name\": \"write_file\", \"arguments\": \"{\\\"path\\\":\\\"b.c\\\"}\"}}"
        "]}";
    append_conversation_message(&history, "assistant", assistant_multi);

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "ok", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Should produce two separate function_call items */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"call_id\":\"call_a\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"call_id\":\"call_b\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"name\":\"read_file\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"name\":\"write_file\""));

    /* Count function_call occurrences */
    int fc_count = 0;
    const char *p = json;
    while ((p = strstr(p, "\"type\":\"function_call\"")) != NULL) {
        fc_count++;
        p++;
    }
    TEST_ASSERT_EQUAL_INT(2, fc_count);

    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_build_request_full_tool_roundtrip(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};

    /* user -> assistant(tool_calls) -> tool(result) using production format */
    append_conversation_message(&history, "user", "Read test.c");

    const char *assistant_tc =
        "{\"role\": \"assistant\", \"content\": null, "
        "\"tool_calls\": [{\"id\": \"call_rt1\", \"type\": \"function\", "
        "\"function\": {\"name\": \"read_file\", "
        "\"arguments\": \"{\\\"path\\\":\\\"test.c\\\"}\"}}]}";
    append_conversation_message(&history, "assistant", assistant_tc);

    append_tool_message(&history, "int main() {}", "call_rt1", "read_file");

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "Now explain it", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Parse the full request to verify ordering and structure */
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    cJSON *input = cJSON_GetObjectItem(root, "input");
    TEST_ASSERT_NOT_NULL(input);

    int arr_size = cJSON_GetArraySize(input);
    /* Expect: user, function_call, function_call_output, user (current msg) */
    TEST_ASSERT_GREATER_OR_EQUAL(4, arr_size);

    /* Find function_call and function_call_output indices */
    int fc_idx = -1, fco_idx = -1;
    for (int i = 0; i < arr_size; i++) {
        cJSON *item = cJSON_GetArrayItem(input, i);
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (type && cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "function_call") == 0) fc_idx = i;
            if (strcmp(type->valuestring, "function_call_output") == 0) fco_idx = i;
        }
    }

    /* function_call must come before function_call_output */
    TEST_ASSERT_NOT_EQUAL(-1, fc_idx);
    TEST_ASSERT_NOT_EQUAL(-1, fco_idx);
    TEST_ASSERT_TRUE(fc_idx < fco_idx);

    /* Both must reference the same call_id */
    cJSON *fc_item = cJSON_GetArrayItem(input, fc_idx);
    cJSON *fco_item = cJSON_GetArrayItem(input, fco_idx);
    cJSON *fc_call_id = cJSON_GetObjectItem(fc_item, "call_id");
    cJSON *fco_call_id = cJSON_GetObjectItem(fco_item, "call_id");
    TEST_ASSERT_NOT_NULL(fc_call_id);
    TEST_ASSERT_NOT_NULL(fco_call_id);
    TEST_ASSERT_EQUAL_STRING("call_rt1", fc_call_id->valuestring);
    TEST_ASSERT_EQUAL_STRING("call_rt1", fco_call_id->valuestring);

    cJSON_Delete(root);
    free(json);
    cleanup_conversation_history(&history);
}

void test_codex_build_request_tool_calls_parse_failure(void) {
    ProviderRegistry *registry = get_provider_registry();
    LLMProvider *provider = detect_provider_for_url(registry, "https://chatgpt.com/backend-api/codex/responses");
    TEST_ASSERT_NOT_NULL(provider);

    ConversationHistory history = {0};
    /* Malformed JSON that contains "tool_calls" but won't parse */
    const char *broken_json = "{\"tool_calls\": [incomplete";
    append_conversation_message(&history, "assistant", broken_json);

    SystemPromptParts prompt = { .base_prompt = "You are helpful.", .dynamic_context = NULL };
    char *json = provider->build_request_json(provider, "codex-mini", &prompt,
                                                &history, "ok", 1024, NULL);
    TEST_ASSERT_NOT_NULL(json);

    /* Fallback: should emit as plain assistant message */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"role\":\"assistant\""));
    /* Should NOT have function_call items */
    TEST_ASSERT_NULL(strstr(json, "\"type\":\"function_call\""));

    free(json);
    cleanup_conversation_history(&history);
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
    RUN_TEST(test_codex_parse_response_error);
    RUN_TEST(test_codex_parse_response_multi_output_text);
    RUN_TEST(test_codex_build_request_with_tools);
    RUN_TEST(test_codex_build_streaming_request);
    RUN_TEST(test_codex_parse_stream_text_delta);
    RUN_TEST(test_codex_parse_stream_completed);
    RUN_TEST(test_codex_parse_stream_tool_call);
    RUN_TEST(test_codex_parse_stream_done_sentinel);
    RUN_TEST(test_codex_parse_stream_null_empty);
    RUN_TEST(test_codex_parse_stream_error_events);
    RUN_TEST(test_codex_build_request_with_tool_calls);
    RUN_TEST(test_codex_build_request_tool_calls_with_content);
    RUN_TEST(test_codex_build_request_tool_calls_null_content);
    RUN_TEST(test_codex_build_request_multiple_tool_calls);
    RUN_TEST(test_codex_build_request_full_tool_roundtrip);
    RUN_TEST(test_codex_build_request_tool_calls_parse_failure);
    RUN_TEST(test_codex_build_request_with_tool_result);
    return UNITY_END();
}
