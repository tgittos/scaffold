#include "unity.h"
#include "session/rolling_summary.h"
#include "session/session_manager.h"
#include "session/conversation_tracker.h"
#include "util/app_home.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
    app_home_init(NULL);
}

void tearDown(void) {
    app_home_cleanup();
}

void test_rolling_summary_init(void) {
    RollingSummary summary;
    rolling_summary_init(&summary);

    TEST_ASSERT_NULL(summary.summary_text);
    TEST_ASSERT_EQUAL_INT(0, summary.estimated_tokens);
    TEST_ASSERT_EQUAL_INT(0, summary.messages_summarized);
}

void test_rolling_summary_init_null(void) {
    rolling_summary_init(NULL);
}

void test_rolling_summary_cleanup(void) {
    RollingSummary summary;
    rolling_summary_init(&summary);

    summary.summary_text = strdup("test summary");
    summary.estimated_tokens = 100;
    summary.messages_summarized = 5;

    rolling_summary_cleanup(&summary);

    TEST_ASSERT_NULL(summary.summary_text);
    TEST_ASSERT_EQUAL_INT(0, summary.estimated_tokens);
    TEST_ASSERT_EQUAL_INT(0, summary.messages_summarized);
}

void test_rolling_summary_cleanup_null(void) {
    rolling_summary_cleanup(NULL);
}

void test_rolling_summary_cleanup_already_null(void) {
    RollingSummary summary;
    rolling_summary_init(&summary);

    rolling_summary_cleanup(&summary);

    TEST_ASSERT_NULL(summary.summary_text);
}

void test_session_data_init_includes_rolling_summary(void) {
    SessionData session;
    session_data_init(&session);

    TEST_ASSERT_NULL(session.rolling_summary.summary_text);
    TEST_ASSERT_EQUAL_INT(0, session.rolling_summary.estimated_tokens);
    TEST_ASSERT_EQUAL_INT(0, session.rolling_summary.messages_summarized);

    session_data_cleanup(&session);
}

void test_session_data_cleanup_cleans_rolling_summary(void) {
    SessionData session;
    session_data_init(&session);

    session.rolling_summary.summary_text = strdup("test");
    session.rolling_summary.estimated_tokens = 50;
    session.rolling_summary.messages_summarized = 3;

    session_data_cleanup(&session);

    TEST_ASSERT_NULL(session.rolling_summary.summary_text);
    TEST_ASSERT_EQUAL_INT(0, session.rolling_summary.estimated_tokens);
}

void test_generate_rolling_summary_null_params(void) {
    char* result = NULL;

    int ret = generate_rolling_summary(NULL, "key", 0, "model", NULL, 0, NULL, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);
    TEST_ASSERT_NULL(result);

    ConversationMessage msg = {.role = "user", .content = "hello"};

    ret = generate_rolling_summary("url", "key", 0, NULL, &msg, 1, NULL, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = generate_rolling_summary("url", "key", 0, "model", NULL, 1, NULL, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = generate_rolling_summary("url", "key", 0, "model", &msg, 0, NULL, &result);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = generate_rolling_summary("url", "key", 0, "model", &msg, 1, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_generate_rolling_summary_invalid_url_fails_gracefully(void) {
    ConversationMessage messages[2];
    messages[0].role = strdup("user");
    messages[0].content = strdup("Hello, can you help me?");
    messages[0].tool_call_id = NULL;
    messages[0].tool_name = NULL;
    messages[1].role = strdup("assistant");
    messages[1].content = strdup("Of course, what do you need?");
    messages[1].tool_call_id = NULL;
    messages[1].tool_name = NULL;

    char* result = NULL;
    int ret = generate_rolling_summary(
        "http://invalid.invalid.invalid:99999/v1/chat/completions",
        "fake-key",
        0,
        "gpt-4",
        messages,
        2,
        NULL,
        &result
    );

    TEST_ASSERT_EQUAL_INT(-1, ret);
    TEST_ASSERT_NULL(result);

    free(messages[0].role);
    free(messages[0].content);
    free(messages[1].role);
    free(messages[1].content);
}

void test_rolling_summary_struct_in_session_data(void) {
    SessionData session;
    session_data_init(&session);

    session.rolling_summary.summary_text = strdup("Earlier we discussed implementing a new feature.");
    session.rolling_summary.estimated_tokens = 12;
    session.rolling_summary.messages_summarized = 5;

    TEST_ASSERT_NOT_NULL(session.rolling_summary.summary_text);
    TEST_ASSERT_EQUAL_STRING("Earlier we discussed implementing a new feature.",
                            session.rolling_summary.summary_text);
    TEST_ASSERT_EQUAL_INT(12, session.rolling_summary.estimated_tokens);
    TEST_ASSERT_EQUAL_INT(5, session.rolling_summary.messages_summarized);

    session_data_cleanup(&session);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_rolling_summary_init);
    RUN_TEST(test_rolling_summary_init_null);
    RUN_TEST(test_rolling_summary_cleanup);
    RUN_TEST(test_rolling_summary_cleanup_null);
    RUN_TEST(test_rolling_summary_cleanup_already_null);
    RUN_TEST(test_session_data_init_includes_rolling_summary);
    RUN_TEST(test_session_data_cleanup_cleans_rolling_summary);
    RUN_TEST(test_generate_rolling_summary_null_params);
    RUN_TEST(test_generate_rolling_summary_invalid_url_fails_gracefully);
    RUN_TEST(test_rolling_summary_struct_in_session_data);

    return UNITY_END();
}
