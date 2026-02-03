#include "unity.h"
#include "messaging/message_poller.h"
#include "ipc/message_store.h"
#include "utils/ralph_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <fcntl.h>

static message_store_t* g_store = NULL;

void setUp(void) {
    ralph_home_init("/tmp/test_poller_home");
    message_store_reset_instance_for_testing();
    g_store = message_store_get_instance();
}

void tearDown(void) {
    message_store_reset_instance_for_testing();
    g_store = NULL;
    ralph_home_cleanup();
}

void test_poller_create_destroy(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);
    message_poller_destroy(poller);
}

void test_poller_create_null_agent_id(void) {
    message_poller_t* poller = message_poller_create(NULL, 100);
    TEST_ASSERT_NULL(poller);
}

void test_poller_create_default_interval(void) {
    message_poller_t* poller = message_poller_create("test-agent", 0);
    TEST_ASSERT_NOT_NULL(poller);
    message_poller_destroy(poller);
}

void test_poller_get_notify_fd(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);

    int fd = message_poller_get_notify_fd(poller);
    TEST_ASSERT_TRUE(fd >= 0);

    message_poller_destroy(poller);
}

void test_poller_get_notify_fd_null(void) {
    int fd = message_poller_get_notify_fd(NULL);
    TEST_ASSERT_EQUAL(-1, fd);
}

void test_poller_start_stop(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);

    int rc = message_poller_start(poller);
    TEST_ASSERT_EQUAL(0, rc);

    message_poller_stop(poller);
    message_poller_destroy(poller);
}

void test_poller_start_twice(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);

    int rc1 = message_poller_start(poller);
    TEST_ASSERT_EQUAL(0, rc1);

    int rc2 = message_poller_start(poller);
    TEST_ASSERT_EQUAL(0, rc2);

    message_poller_stop(poller);
    message_poller_destroy(poller);
}

void test_poller_stop_without_start(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);

    message_poller_stop(poller);
    message_poller_destroy(poller);
}

void test_poller_get_pending_no_messages(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);

    pending_message_counts_t counts = {0};
    int rc = message_poller_get_pending(poller, &counts);
    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_EQUAL(0, counts.direct_count);
    TEST_ASSERT_EQUAL(0, counts.channel_count);

    message_poller_destroy(poller);
}

void test_poller_clear_notification(void) {
    message_poller_t* poller = message_poller_create("test-agent", 100);
    TEST_ASSERT_NOT_NULL(poller);

    int rc = message_poller_clear_notification(poller);
    TEST_ASSERT_EQUAL(0, rc);

    message_poller_destroy(poller);
}

void test_poller_detects_pending_direct_message(void) {
    const char* agent_id = "polling-agent";

    char msg_id[40] = {0};
    int rc = message_send_direct(g_store, "sender", agent_id, "Hello poller!", 0, msg_id);
    TEST_ASSERT_EQUAL(0, rc);

    message_poller_t* poller = message_poller_create(agent_id, 50);
    TEST_ASSERT_NOT_NULL(poller);

    rc = message_poller_start(poller);
    TEST_ASSERT_EQUAL(0, rc);

    int notify_fd = message_poller_get_notify_fd(poller);
    TEST_ASSERT_TRUE(notify_fd >= 0);

    fd_set read_fds;
    struct timeval timeout;
    int ready = 0;

    for (int attempt = 0; attempt < 10 && ready <= 0; attempt++) {
        FD_ZERO(&read_fds);
        FD_SET(notify_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        ready = select(notify_fd + 1, &read_fds, NULL, NULL, &timeout);
    }

    TEST_ASSERT_TRUE(ready > 0);
    TEST_ASSERT_TRUE(FD_ISSET(notify_fd, &read_fds));

    message_poller_stop(poller);
    message_poller_destroy(poller);
}

void test_poller_detects_pending_channel_message(void) {
    const char* agent_id = "channel-poller";

    channel_create(g_store, "poller-channel", "Test channel", "creator", 0);
    channel_subscribe(g_store, "poller-channel", agent_id);

    char msg_id[40] = {0};
    channel_publish(g_store, "poller-channel", "publisher", "Channel message", msg_id);

    message_poller_t* poller = message_poller_create(agent_id, 50);
    TEST_ASSERT_NOT_NULL(poller);

    int rc = message_poller_start(poller);
    TEST_ASSERT_EQUAL(0, rc);

    int notify_fd = message_poller_get_notify_fd(poller);

    fd_set read_fds;
    struct timeval timeout;
    int ready = 0;

    for (int attempt = 0; attempt < 10 && ready <= 0; attempt++) {
        FD_ZERO(&read_fds);
        FD_SET(notify_fd, &read_fds);
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        ready = select(notify_fd + 1, &read_fds, NULL, NULL, &timeout);
    }

    TEST_ASSERT_TRUE(ready > 0);

    message_poller_stop(poller);
    message_poller_destroy(poller);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_poller_create_destroy);
    RUN_TEST(test_poller_create_null_agent_id);
    RUN_TEST(test_poller_create_default_interval);
    RUN_TEST(test_poller_get_notify_fd);
    RUN_TEST(test_poller_get_notify_fd_null);
    RUN_TEST(test_poller_start_stop);
    RUN_TEST(test_poller_start_twice);
    RUN_TEST(test_poller_stop_without_start);
    RUN_TEST(test_poller_get_pending_no_messages);
    RUN_TEST(test_poller_clear_notification);
    RUN_TEST(test_poller_detects_pending_direct_message);
    RUN_TEST(test_poller_detects_pending_channel_message);

    return UNITY_END();
}
