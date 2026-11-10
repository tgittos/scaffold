#include "network/api_common.h"
#include "session/conversation_tracker.h"
#include "unity.h"
#include "util/app_home.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {
    app_home_init(NULL);
}

void tearDown(void) {
    app_home_cleanup();
}

static char* clone(const char* s) {
    char* c = malloc(strlen(s) + 1);
    if (c) strcpy(c, s);
    return c;
}

// Canonical OpenAI tool result: {"role":"tool","tool_call_id":...,"content":...}
void test_tool_message_canonical_shape(void) {
    ConversationMessage message = {
        .role = "tool",
        .content = "{\"success\":true,\"output\":\"42\"}",
        .tool_call_id = "call_abc123",
        .tool_name = "shell"
    };

    char buffer[4096] = {0};
    int written = format_openai_message(buffer, sizeof(buffer), &message, 1);
    TEST_ASSERT_TRUE(written > 0);

    cJSON* json = cJSON_Parse(buffer);
    TEST_ASSERT_NOT_NULL(json);
    if (json) {
        cJSON* role = cJSON_GetObjectItem(json, "role");
        TEST_ASSERT_NOT_NULL(role);
        TEST_ASSERT_EQUAL_STRING("tool", role->valuestring);

        cJSON* id = cJSON_GetObjectItem(json, "tool_call_id");
        TEST_ASSERT_NOT_NULL(id);
        TEST_ASSERT_EQUAL_STRING("call_abc123", id->valuestring);

        cJSON* content = cJSON_GetObjectItem(json, "content");
        TEST_ASSERT_NOT_NULL(content);
        TEST_ASSERT_EQUAL_STRING("{\"success\":true,\"output\":\"42\"}", content->valuestring);

        // The result must not be demoted to a system message with a decorative prefix
        TEST_ASSERT_NULL(strstr(buffer, "[Tool"));
    }
    cJSON_Delete(json);
}

static const char* CANONICAL_ASSISTANT =
    "{\"role\": \"assistant\", \"content\": \"Reading it.\", "
    "\"tool_calls\": [{\"id\": \"call_1\", \"type\": \"function\", "
    "\"function\": {\"name\": \"read_file\", \"arguments\": \"{\\\"path\\\": \\\"x.md\\\"}\"}}]}";

// Assistant messages stored with serialized tool_calls are emitted verbatim,
// so the API receives the model's own tool calls (not a text summary).
void test_assistant_tool_message_emitted_verbatim(void) {
    ConversationMessage message = {
        .role = "assistant",
        .content = (char*)CANONICAL_ASSISTANT,
        .has_tool_calls = 1
    };

    char buffer[8192] = {0};
    int written = format_openai_message(buffer, sizeof(buffer), &message, 1);
    TEST_ASSERT_TRUE(written > 0);

    cJSON* json = cJSON_Parse(buffer);
    TEST_ASSERT_NOT_NULL(json);
    if (json) {
        cJSON* role = cJSON_GetObjectItem(json, "role");
        TEST_ASSERT_NOT_NULL(role);
        TEST_ASSERT_EQUAL_STRING("assistant", role->valuestring);

        cJSON* tcs = cJSON_GetObjectItem(json, "tool_calls");
        TEST_ASSERT_NOT_NULL(tcs);
        TEST_ASSERT_TRUE(cJSON_IsArray(tcs));
        TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(tcs));

        cJSON* fn = cJSON_GetObjectItem(cJSON_GetArrayItem(tcs, 0), "function");
        TEST_ASSERT_NOT_NULL(fn);
        cJSON* name = cJSON_GetObjectItem(fn, "name");
        TEST_ASSERT_EQUAL_STRING("read_file", name->valuestring);
    }
    cJSON_Delete(json);
}

// Same for assistant messages from older sessions: canonical JSON content
// without the has_tool_calls flag must still be emitted verbatim.
void test_assistant_tool_message_without_flag(void) {
    ConversationMessage message = {
        .role = "assistant",
        .content = (char*)CANONICAL_ASSISTANT,
        .has_tool_calls = 0
    };

    char buffer[8192] = {0};
    int written = format_openai_message(buffer, sizeof(buffer), &message, 1);
    TEST_ASSERT_TRUE(written > 0);

    cJSON* json = cJSON_Parse(buffer);
    TEST_ASSERT_NOT_NULL(json);
    if (json) {
        cJSON* tcs = cJSON_GetObjectItem(json, "tool_calls");
        TEST_ASSERT_NOT_NULL(tcs);
        TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(tcs));
    }
    cJSON_Delete(json);
}

// Plain user messages are unchanged.
void test_user_message_plain(void) {
    ConversationMessage message = {
        .role = "user",
        .content = "hello"
    };

    char buffer[1024] = {0};
    int written = format_openai_message(buffer, sizeof(buffer), &message, 1);
    TEST_ASSERT_TRUE(written > 0);

    cJSON* json = cJSON_Parse(buffer);
    TEST_ASSERT_NOT_NULL(json);
    if (json) {
        TEST_ASSERT_EQUAL_STRING("user", cJSON_GetObjectItem(json, "role")->valuestring);
        TEST_ASSERT_EQUAL_STRING("hello", cJSON_GetObjectItem(json, "content")->valuestring);
    }
    cJSON_Delete(json);
}

// A full follow-up round (user, assistant+tool_calls, tool result) must
// serialize as the canonical OpenAI shape end to end.
void test_build_messages_canonical_roundtrip(void) {
    ConversationHistory history;
    init_conversation_history(&history);

    append_conversation_message(&history, "user", "Read x.md");
    append_conversation_message(&history, "assistant", CANONICAL_ASSISTANT);
    char* result = clone("{\"success\":true,\"content\":\"the file\"}");
    append_tool_message(&history, result, "call_1", "read_file");
    free(result);

    SystemPromptParts sys = { .base_prompt = "be nice", .dynamic_context = NULL };
    char buffer[16384] = {0};
    int len = build_messages_json(buffer, sizeof(buffer), &sys, &history,
                                  NULL, format_openai_message, 0);
    TEST_ASSERT_TRUE(len > 0);

    char* full = malloc((size_t)len + 3);
    snprintf(full, (size_t)len + 3, "[%.*s]", len, buffer);

    cJSON* arr = cJSON_Parse(full);
    free(full);
    TEST_ASSERT_NOT_NULL(arr);
    if (arr) {
        /* system + user + assistant(tool_calls) + tool result */
        TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(arr));

        cJSON* sysmsg = cJSON_GetArrayItem(arr, 0);
        TEST_ASSERT_EQUAL_STRING("system", cJSON_GetObjectItem(sysmsg, "role")->valuestring);

        cJSON* user = cJSON_GetArrayItem(arr, 1);
        TEST_ASSERT_EQUAL_STRING("user", cJSON_GetObjectItem(user, "role")->valuestring);

        cJSON* assistant = cJSON_GetArrayItem(arr, 2);
        TEST_ASSERT_EQUAL_STRING("assistant", cJSON_GetObjectItem(assistant, "role")->valuestring);
        cJSON* tcs = cJSON_GetObjectItem(assistant, "tool_calls");
        TEST_ASSERT_NOT_NULL(tcs);
        TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(tcs));

        cJSON* tool = cJSON_GetArrayItem(arr, 3);
        cJSON* role = cJSON_GetObjectItem(tool, "role");
        TEST_ASSERT_EQUAL_STRING("tool", role->valuestring);
        cJSON* id = cJSON_GetObjectItem(tool, "tool_call_id");
        TEST_ASSERT_EQUAL_STRING("call_1", id->valuestring);
    }
    cJSON_Delete(arr);

    cleanup_conversation_history(&history);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_tool_message_canonical_shape);
    RUN_TEST(test_assistant_tool_message_emitted_verbatim);
    RUN_TEST(test_assistant_tool_message_without_flag);
    RUN_TEST(test_user_message_plain);
    RUN_TEST(test_build_messages_canonical_roundtrip);

    return UNITY_END();
}
