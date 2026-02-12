#include "unity.h"
#include "ipc/notification_formatter.h"
#include "ipc/message_store.h"
#include "services/services.h"
#include "util/app_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static message_store_t* g_store = NULL;

static void rmdir_recursive(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char full_path[1024] = {0};
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                rmdir_recursive(full_path);
            } else {
                unlink(full_path);
            }
        }
    }
    closedir(dir);
    rmdir(path);
}

void setUp(void) {
    // Clean up any leftover test data
    rmdir_recursive("/tmp/test_formatter_home");
    app_home_init("/tmp/test_formatter_home");
    g_store = message_store_create(NULL);
}

void tearDown(void) {
    if (g_store) {
        message_store_destroy(g_store);
        g_store = NULL;
    }
    app_home_cleanup();
}

void test_bundle_create_null_agent(void) {
    notification_bundle_t* bundle = notification_bundle_create(NULL, NULL);
    TEST_ASSERT_NULL(bundle);
}

void test_bundle_create_empty(void) {
    notification_bundle_t* bundle = notification_bundle_create("test-agent", NULL);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(0, notification_bundle_total_count(bundle));
    notification_bundle_destroy(bundle);
}

void test_bundle_create_with_injected_services(void) {
    Services* svc = services_create_empty();
    TEST_ASSERT_NOT_NULL(svc);
    svc->message_store = g_store;

    const char* agent_id = "injected-agent";
    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", agent_id, "Test message", 0, msg_id);

    notification_bundle_t* bundle = notification_bundle_create(agent_id, svc);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(1, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
    services_destroy(svc);
}

void test_bundle_with_direct_messages(void) {
    const char* agent_id = "format-agent";

    char msg_id[40] = {0};
    message_send_direct(g_store, "sender-1", agent_id, "Message one", 0, msg_id);
    message_send_direct(g_store, "sender-2", agent_id, "Message two", 0, msg_id);

    Services* svc = services_create_empty();
    TEST_ASSERT_NOT_NULL(svc);
    svc->message_store = g_store;

    notification_bundle_t* bundle = notification_bundle_create(agent_id, svc);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(2, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
    svc->message_store = NULL;
    services_destroy(svc);
}

void test_bundle_with_channel_messages(void) {
    const char* agent_id = "channel-format-agent";

    channel_create(g_store, "format-channel", "Format test", "creator", 0);
    channel_subscribe(g_store, "format-channel", agent_id);

    char msg_id[40] = {0};
    channel_publish(g_store, "format-channel", "publisher", "Channel message", msg_id);

    Services* svc = services_create_empty();
    TEST_ASSERT_NOT_NULL(svc);
    svc->message_store = g_store;

    notification_bundle_t* bundle = notification_bundle_create(agent_id, svc);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(1, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
    svc->message_store = NULL;
    services_destroy(svc);
}

void test_bundle_with_mixed_messages(void) {
    const char* agent_id = "mixed-format-agent";

    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", agent_id, "Direct message", 0, msg_id);

    channel_create(g_store, "mixed-channel", "Mixed test", "creator", 0);
    channel_subscribe(g_store, "mixed-channel", agent_id);
    channel_publish(g_store, "mixed-channel", "publisher", "Channel message", msg_id);

    Services* svc = services_create_empty();
    TEST_ASSERT_NOT_NULL(svc);
    svc->message_store = g_store;

    notification_bundle_t* bundle = notification_bundle_create(agent_id, svc);
    TEST_ASSERT_NOT_NULL(bundle);
    TEST_ASSERT_EQUAL(2, notification_bundle_total_count(bundle));

    notification_bundle_destroy(bundle);
    svc->message_store = NULL;
    services_destroy(svc);
}

void test_format_for_llm_null_bundle(void) {
    char* formatted = notification_format_for_llm(NULL);
    TEST_ASSERT_NULL(formatted);
}

void test_format_for_llm_empty_bundle(void) {
    notification_bundle_t* bundle = notification_bundle_create("empty-agent", NULL);
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

    Services* svc = services_create_empty();
    TEST_ASSERT_NOT_NULL(svc);
    svc->message_store = g_store;

    notification_bundle_t* bundle = notification_bundle_create(agent_id, svc);
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
    svc->message_store = NULL;
    services_destroy(svc);
}

void test_bundle_total_count_null(void) {
    int count = notification_bundle_total_count(NULL);
    TEST_ASSERT_EQUAL(0, count);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_bundle_create_null_agent);
    RUN_TEST(test_bundle_create_empty);
    RUN_TEST(test_bundle_create_with_injected_services);
    RUN_TEST(test_bundle_with_direct_messages);
    RUN_TEST(test_bundle_with_channel_messages);
    RUN_TEST(test_bundle_with_mixed_messages);
    RUN_TEST(test_format_for_llm_null_bundle);
    RUN_TEST(test_format_for_llm_empty_bundle);
    RUN_TEST(test_format_for_llm_with_messages);
    RUN_TEST(test_bundle_total_count_null);

    return UNITY_END();
}
