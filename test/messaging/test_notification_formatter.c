#include "unity.h"
#include "messaging/notification_formatter.h"
#include "ipc/message_store.h"
#include "utils/ralph_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static message_store_t* g_store = NULL;

void setUp(void) {
    ralph_home_init("/tmp/test_formatter_home");
    message_store_reset_instance_for_testing();
    g_store = message_store_get_instance();
}

void tearDown(void) {
    message_store_reset_instance_for_testing();
    g_store = NULL;
    ralph_home_cleanup();
}

void test_bundle_create_null_agent(void) {
    notification_bundle_t* bundle = notification_bundle_create(NULL);
    TEST_ASSERT_NULL(bundle);
}

void test_bundle_create_empty(void) {
    notification_bundle_t* bundle = notification_bundle_create("test-agent");
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(0, notification_bundle_total_count(bundle));
    notification_bundle_destroy(bundle);
}

void test_bundle_with_direct_messages(void) {
    const char* agent_id = "format-agent";

    char msg_id[40] = {0};
    message_send_direct(g_store, "sender-1", agent_id, "Message one", 0, msg_id);
    message_send_direct(g_store, "sender-2", agent_id, "Message two", 0, msg_id);

    notification_bundle_t* bundle = notification_bundle_create(agent_id);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(2, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
}

void test_bundle_with_channel_messages(void) {
    const char* agent_id = "channel-format-agent";

    channel_create(g_store, "format-channel", "Format test", "creator", 0);
    channel_subscribe(g_store, "format-channel", agent_id);

    char msg_id[40] = {0};
    channel_publish(g_store, "format-channel", "publisher", "Channel message", msg_id);

    notification_bundle_t* bundle = notification_bundle_create(agent_id);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(1, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
}

void test_bundle_with_mixed_messages(void) {
    const char* agent_id = "mixed-format-agent";

    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", agent_id, "Direct message", 0, msg_id);

    channel_create(g_store, "mixed-channel", "Mixed test", "creator", 0);
    channel_subscribe(g_store, "mixed-channel", agent_id);
    channel_publish(g_store, "mixed-channel", "publisher", "Channel message", msg_id);

    notification_bundle_t* bundle = notification_bundle_create(agent_id);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(2, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
}

void test_format_for_llm_null_bundle(void) {
    char* formatted = notification_format_for_llm(NULL);
    TEST_ASSERT_NULL(formatted);
}

void test_format_for_llm_empty_bundle(void) {
    notification_bundle_t* bundle = notification_bundle_create("empty-agent");
    TEST_ASSERT_NOT_NULL(bundle);

    char* formatted = notification_format_for_llm(bundle);
    TEST_ASSERT_NULL(formatted);

    notification_bundle_destroy(bundle);
}

void test_format_for_llm_with_messages(void) {
    const char* agent_id = "llm-format-agent";

    char msg_id[40] = {0};
    message_send_direct(g_store, "sender-agent", agent_id, "Hello from sender", 0, msg_id);

    channel_create(g_store, "llm-channel", "LLM test", "creator", 0);
    channel_subscribe(g_store, "llm-channel", agent_id);
    channel_publish(g_store, "llm-channel", "channel-sender", "Channel broadcast", msg_id);

    notification_bundle_t* bundle = notification_bundle_create(agent_id);
    TEST_ASSERT_NOT_NULL(bundle);

    char* formatted = notification_format_for_llm(bundle);
    TEST_ASSERT_NOT_NULL(formatted);

    TEST_ASSERT_NOT_NULL(strstr(formatted, "[INCOMING AGENT MESSAGES]"));
    TEST_ASSERT_NOT_NULL(strstr(formatted, "Direct from sender-agent"));
    TEST_ASSERT_NOT_NULL(strstr(formatted, "Hello from sender"));
    TEST_ASSERT_NOT_NULL(strstr(formatted, "Channel #llm-channel"));
    TEST_ASSERT_NOT_NULL(strstr(formatted, "Channel broadcast"));
    TEST_ASSERT_NOT_NULL(strstr(formatted, "Please review and respond"));

    free(formatted);
    notification_bundle_destroy(bundle);
}

void test_bundle_total_count_null(void) {
    int count = notification_bundle_total_count(NULL);
    TEST_ASSERT_EQUAL(0, count);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_bundle_create_null_agent);
    RUN_TEST(test_bundle_create_empty);
    RUN_TEST(test_bundle_with_direct_messages);
    RUN_TEST(test_bundle_with_channel_messages);
    RUN_TEST(test_bundle_with_mixed_messages);
    RUN_TEST(test_format_for_llm_null_bundle);
    RUN_TEST(test_format_for_llm_empty_bundle);
    RUN_TEST(test_format_for_llm_with_messages);
    RUN_TEST(test_bundle_total_count_null);

    return UNITY_END();
}
