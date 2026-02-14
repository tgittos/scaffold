#include "unity.h"
#include "ipc/message_store.h"
#include "util/uuid_utils.h"
#include "util/app_home.h"
#include "../test_fs_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>

static char g_test_db_path[256];
static char g_test_home_dir[256];
static message_store_t* g_store = NULL;

void setUp(void) {
    snprintf(g_test_db_path, sizeof(g_test_db_path), "/tmp/test_messages_%d.db", getpid());
    snprintf(g_test_home_dir, sizeof(g_test_home_dir), "/tmp/test_message_store_home_%d", getpid());
    app_home_init(g_test_home_dir);
    unlink_sqlite_db(g_test_db_path);
    g_store = message_store_create(g_test_db_path);
}

void tearDown(void) {
    if (g_store != NULL) {
        message_store_destroy(g_store);
        g_store = NULL;
    }
    unlink_sqlite_db(g_test_db_path);
    app_home_cleanup();
}

void test_message_store_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(g_store);
}

void test_message_store_multiple_instances(void) {
    // Can create separate instances with different paths
    char other_path[256];
    snprintf(other_path, sizeof(other_path), "/tmp/test_messages_other_%d.db", getpid());
    message_store_t* store2 = message_store_create(other_path);
    TEST_ASSERT_NOT_NULL(store2);
    TEST_ASSERT_NOT_EQUAL(g_store, store2);

    message_store_destroy(store2);
    unlink_sqlite_db(other_path);
}

void test_send_direct_message(void) {
    char msg_id[40] = {0};
    int rc = message_send_direct(g_store, "agent-1", "agent-2", "Hello agent-2!", 0, msg_id);
    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_TRUE(uuid_is_valid(msg_id));
}

void test_send_direct_message_null_params(void) {
    char msg_id[40] = {0};

    int rc = message_send_direct(NULL, "agent-1", "agent-2", "content", 0, msg_id);
    TEST_ASSERT_EQUAL(-1, rc);

    rc = message_send_direct(g_store, NULL, "agent-2", "content", 0, msg_id);
    TEST_ASSERT_EQUAL(-1, rc);

    rc = message_send_direct(g_store, "agent-1", NULL, "content", 0, msg_id);
    TEST_ASSERT_EQUAL(-1, rc);

    rc = message_send_direct(g_store, "agent-1", "agent-2", NULL, 0, msg_id);
    TEST_ASSERT_EQUAL(-1, rc);

    rc = message_send_direct(g_store, "agent-1", "agent-2", "content", 0, NULL);
    TEST_ASSERT_EQUAL(-1, rc);
}

void test_receive_direct_messages(void) {
    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", "receiver", "Message 1", 0, msg_id);
    message_send_direct(g_store, "sender", "receiver", "Message 2", 0, msg_id);
    message_send_direct(g_store, "other", "someone", "Not for receiver", 0, msg_id);

    size_t count = 0;
    DirectMessage** msgs = message_receive_direct(g_store, "receiver", 10, &count);
    TEST_ASSERT_NOT_NULL(msgs);
    TEST_ASSERT_EQUAL(2, count);

    TEST_ASSERT_EQUAL_STRING("sender", msgs[0]->sender_id);
    TEST_ASSERT_EQUAL_STRING("receiver", msgs[0]->recipient_id);
    TEST_ASSERT_EQUAL_STRING("Message 1", msgs[0]->content);

    TEST_ASSERT_EQUAL_STRING("Message 2", msgs[1]->content);

    direct_message_free_list(msgs, count);
}

void test_receive_marks_as_read(void) {
    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", "receiver", "Test message", 0, msg_id);

    size_t count1 = 0;
    DirectMessage** msgs1 = message_receive_direct(g_store, "receiver", 10, &count1);
    TEST_ASSERT_EQUAL(1, count1);
    direct_message_free_list(msgs1, count1);

    size_t count2 = 0;
    DirectMessage** msgs2 = message_receive_direct(g_store, "receiver", 10, &count2);
    TEST_ASSERT_NULL(msgs2);
    TEST_ASSERT_EQUAL(0, count2);
}

void test_message_has_pending(void) {
    int pending = message_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(0, pending);

    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", "agent-1", "Hello", 0, msg_id);

    pending = message_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(1, pending);

    size_t count = 0;
    DirectMessage** msgs = message_receive_direct(g_store, "agent-1", 10, &count);
    direct_message_free_list(msgs, count);

    pending = message_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(0, pending);
}

void test_message_get_direct(void) {
    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", "receiver", "Test content", 0, msg_id);

    DirectMessage* msg = message_get_direct(g_store, msg_id);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL_STRING(msg_id, msg->id);
    TEST_ASSERT_EQUAL_STRING("sender", msg->sender_id);
    TEST_ASSERT_EQUAL_STRING("receiver", msg->recipient_id);
    TEST_ASSERT_EQUAL_STRING("Test content", msg->content);

    direct_message_free(msg);
}

void test_send_message_with_ttl(void) {
    char msg_id[40] = {0};
    int rc = message_send_direct(g_store, "agent-1", "agent-2", "Expiring message", 3600, msg_id);
    TEST_ASSERT_EQUAL(0, rc);

    DirectMessage* msg = message_get_direct(g_store, msg_id);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(msg->expires_at > 0);
    direct_message_free(msg);
}

void test_channel_create(void) {
    int rc = channel_create(g_store, "test-channel", "A test channel", "creator-1", 0);
    TEST_ASSERT_EQUAL(0, rc);

    Channel* ch = channel_get(g_store, "test-channel");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL_STRING("test-channel", ch->id);
    TEST_ASSERT_EQUAL_STRING("A test channel", ch->description);
    TEST_ASSERT_EQUAL_STRING("creator-1", ch->creator_id);
    TEST_ASSERT_EQUAL(0, ch->is_persistent);

    channel_free(ch);
}

void test_channel_create_persistent(void) {
    int rc = channel_create(g_store, "persist-channel", "Persistent", "creator", 1);
    TEST_ASSERT_EQUAL(0, rc);

    Channel* ch = channel_get(g_store, "persist-channel");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL(1, ch->is_persistent);
    channel_free(ch);
}

void test_channel_list(void) {
    channel_create(g_store, "channel-a", "Channel A", "creator", 0);
    channel_create(g_store, "channel-b", "Channel B", "creator", 0);

    size_t count = 0;
    Channel** channels = channel_list(g_store, &count);
    TEST_ASSERT_NOT_NULL(channels);
    TEST_ASSERT_EQUAL(2, count);

    channel_free_list(channels, count);
}

void test_channel_delete(void) {
    channel_create(g_store, "delete-me", "To delete", "creator", 0);

    int rc = channel_delete(g_store, "delete-me");
    TEST_ASSERT_EQUAL(0, rc);

    Channel* ch = channel_get(g_store, "delete-me");
    TEST_ASSERT_NULL(ch);
}

void test_channel_subscribe(void) {
    channel_create(g_store, "sub-channel", "Subscription test", "creator", 0);

    int subscribed = channel_is_subscribed(g_store, "sub-channel", "agent-1");
    TEST_ASSERT_EQUAL(0, subscribed);

    int rc = channel_subscribe(g_store, "sub-channel", "agent-1");
    TEST_ASSERT_EQUAL(0, rc);

    subscribed = channel_is_subscribed(g_store, "sub-channel", "agent-1");
    TEST_ASSERT_EQUAL(1, subscribed);
}

void test_channel_unsubscribe(void) {
    channel_create(g_store, "unsub-channel", "Unsubscription test", "creator", 0);
    channel_subscribe(g_store, "unsub-channel", "agent-1");

    int rc = channel_unsubscribe(g_store, "unsub-channel", "agent-1");
    TEST_ASSERT_EQUAL(0, rc);

    int subscribed = channel_is_subscribed(g_store, "unsub-channel", "agent-1");
    TEST_ASSERT_EQUAL(0, subscribed);
}

void test_channel_get_subscribers(void) {
    channel_create(g_store, "multi-sub", "Multi subscriber", "creator", 0);
    channel_subscribe(g_store, "multi-sub", "agent-1");
    channel_subscribe(g_store, "multi-sub", "agent-2");
    channel_subscribe(g_store, "multi-sub", "agent-3");

    size_t count = 0;
    char** subscribers = channel_get_subscribers(g_store, "multi-sub", &count);
    TEST_ASSERT_NOT_NULL(subscribers);
    TEST_ASSERT_EQUAL(3, count);

    channel_subscribers_free(subscribers, count);
}

void test_channel_get_agent_subscriptions(void) {
    channel_create(g_store, "ch-1", "Channel 1", "creator", 0);
    channel_create(g_store, "ch-2", "Channel 2", "creator", 0);
    channel_subscribe(g_store, "ch-1", "agent-1");
    channel_subscribe(g_store, "ch-2", "agent-1");

    size_t count = 0;
    char** channels = channel_get_agent_subscriptions(g_store, "agent-1", &count);
    TEST_ASSERT_NOT_NULL(channels);
    TEST_ASSERT_EQUAL(2, count);

    channel_subscriptions_free(channels, count);
}

void test_channel_publish(void) {
    channel_create(g_store, "pub-channel", "Publish test", "creator", 0);

    char msg_id[40] = {0};
    int rc = channel_publish(g_store, "pub-channel", "publisher", "Broadcast message", msg_id);
    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_TRUE(uuid_is_valid(msg_id));
}

void test_channel_receive(void) {
    channel_create(g_store, "recv-channel", "Receive test", "creator", 0);
    channel_subscribe(g_store, "recv-channel", "subscriber");

    char msg_id[40] = {0};
    channel_publish(g_store, "recv-channel", "publisher", "Message 1", msg_id);
    channel_publish(g_store, "recv-channel", "publisher", "Message 2", msg_id);

    size_t count = 0;
    ChannelMessage** msgs = channel_receive(g_store, "recv-channel", "subscriber", 10, &count);
    TEST_ASSERT_NOT_NULL(msgs);
    TEST_ASSERT_EQUAL(2, count);

    TEST_ASSERT_EQUAL_STRING("recv-channel", msgs[0]->channel_id);
    TEST_ASSERT_EQUAL_STRING("publisher", msgs[0]->sender_id);
    TEST_ASSERT_EQUAL_STRING("Message 1", msgs[0]->content);

    channel_message_free_list(msgs, count);
}

void test_channel_receive_updates_last_read(void) {
    channel_create(g_store, "read-channel", "Last read test", "creator", 0);
    channel_subscribe(g_store, "read-channel", "subscriber");

    char msg_id[40] = {0};
    channel_publish(g_store, "read-channel", "publisher", "First message", msg_id);

    size_t count1 = 0;
    ChannelMessage** msgs1 = channel_receive(g_store, "read-channel", "subscriber", 10, &count1);
    TEST_ASSERT_EQUAL(1, count1);
    channel_message_free_list(msgs1, count1);

    size_t count2 = 0;
    ChannelMessage** msgs2 = channel_receive(g_store, "read-channel", "subscriber", 10, &count2);
    TEST_ASSERT_NULL(msgs2);
    TEST_ASSERT_EQUAL(0, count2);

    channel_publish(g_store, "read-channel", "publisher", "New message", msg_id);

    size_t count3 = 0;
    ChannelMessage** msgs3 = channel_receive(g_store, "read-channel", "subscriber", 10, &count3);
    TEST_ASSERT_EQUAL(1, count3);
    TEST_ASSERT_EQUAL_STRING("New message", msgs3[0]->content);
    channel_message_free_list(msgs3, count3);
}

void test_channel_receive_all(void) {
    channel_create(g_store, "all-ch-1", "All channel 1", "creator", 0);
    channel_create(g_store, "all-ch-2", "All channel 2", "creator", 0);
    channel_subscribe(g_store, "all-ch-1", "subscriber");
    channel_subscribe(g_store, "all-ch-2", "subscriber");

    char msg_id[40] = {0};
    channel_publish(g_store, "all-ch-1", "pub1", "From channel 1", msg_id);
    channel_publish(g_store, "all-ch-2", "pub2", "From channel 2", msg_id);

    size_t count = 0;
    ChannelMessage** msgs = channel_receive_all(g_store, "subscriber", 10, &count);
    TEST_ASSERT_NOT_NULL(msgs);
    TEST_ASSERT_EQUAL(2, count);

    channel_message_free_list(msgs, count);
}

void test_message_cleanup_read(void) {
    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", "receiver", "Old message", 0, msg_id);

    size_t count = 0;
    DirectMessage** msgs = message_receive_direct(g_store, "receiver", 10, &count);
    direct_message_free_list(msgs, count);

    int deleted = message_cleanup_read(g_store, -1);
    TEST_ASSERT_TRUE(deleted >= 1);
}

void test_message_cleanup_expired(void) {
    char msg_id[40] = {0};
    message_send_direct(g_store, "sender", "receiver", "Expired message", 1, msg_id);

    sleep(2);

    int deleted = message_cleanup_expired(g_store);
    TEST_ASSERT_TRUE(deleted >= 1);
}

void test_message_cleanup_agent(void) {
    channel_create(g_store, "cleanup-channel", "Cleanup test", "creator", 0);
    channel_subscribe(g_store, "cleanup-channel", "leaving-agent");

    char msg_id[40] = {0};
    message_send_direct(g_store, "leaving-agent", "other", "Sent message", 0, msg_id);
    message_send_direct(g_store, "other", "leaving-agent", "Received message", 0, msg_id);

    int rc = message_cleanup_agent(g_store, "leaving-agent");
    TEST_ASSERT_EQUAL(0, rc);

    int subscribed = channel_is_subscribed(g_store, "cleanup-channel", "leaving-agent");
    TEST_ASSERT_EQUAL(0, subscribed);
}

void test_message_cleanup_channel_messages(void) {
    channel_create(g_store, "old-ch", "Old messages", "creator", 0);
    channel_create(g_store, "persist-ch", "Persistent", "creator", 1);

    char msg_id[40] = {0};
    channel_publish(g_store, "old-ch", "pub", "Old non-persistent", msg_id);
    channel_publish(g_store, "persist-ch", "pub", "Persistent message", msg_id);

    int deleted = message_cleanup_channel_messages(g_store, -1);
    TEST_ASSERT_TRUE(deleted >= 1);
}

void test_cross_process_access(void) {
    channel_create(g_store, "cross-process", "Cross process test", "creator", 0);

    char msg_id[40] = {0};
    message_send_direct(g_store, "parent", "child", "Message from parent", 0, msg_id);

    pid_t pid = fork();
    if (pid == 0) {
        message_store_t* child_store = message_store_create(g_test_db_path);
        if (child_store == NULL) {
            _exit(1);
        }

        size_t count = 0;
        DirectMessage** msgs = message_receive_direct(child_store, "child", 10, &count);
        int success = (msgs != NULL && count == 1 &&
                       strcmp(msgs[0]->content, "Message from parent") == 0);

        direct_message_free_list(msgs, count);
        message_store_destroy(child_store);
        _exit(success ? 0 : 2);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        TEST_ASSERT_EQUAL(0, WEXITSTATUS(status));
    } else {
        TEST_FAIL_MESSAGE("Fork failed");
    }
}

void test_channel_delete_cascades(void) {
    channel_create(g_store, "cascade-ch", "Cascade test", "creator", 0);
    channel_subscribe(g_store, "cascade-ch", "agent-1");

    char msg_id[40] = {0};
    channel_publish(g_store, "cascade-ch", "pub", "Will be deleted", msg_id);

    channel_delete(g_store, "cascade-ch");

    int subscribed = channel_is_subscribed(g_store, "cascade-ch", "agent-1");
    TEST_ASSERT_EQUAL(0, subscribed);
}

void test_peek_pending_empty(void) {
    PendingMessage* msg = message_peek_pending(g_store, "nobody");
    TEST_ASSERT_NULL(msg);
}

void test_peek_pending_returns_oldest(void) {
    char id1[40] = {0}, id2[40] = {0};
    message_send_direct(g_store, "alice", "bob", "First message", 0, id1);
    message_send_direct(g_store, "alice", "bob", "Second message", 0, id2);

    PendingMessage* msg = message_peek_pending(g_store, "bob");
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL_STRING(id1, msg->id);
    TEST_ASSERT_EQUAL_STRING("alice", msg->from);
    TEST_ASSERT_EQUAL_STRING("First message", msg->content);
    TEST_ASSERT_TRUE(msg->timestamp > 0);
    pending_message_free(msg);
}

void test_peek_pending_does_not_consume(void) {
    char id[40] = {0};
    message_send_direct(g_store, "alice", "bob", "Peek me", 0, id);

    PendingMessage* first = message_peek_pending(g_store, "bob");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_STRING(id, first->id);
    pending_message_free(first);

    PendingMessage* second = message_peek_pending(g_store, "bob");
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_EQUAL_STRING(id, second->id);
    pending_message_free(second);
}

void test_consume_marks_as_read(void) {
    char id[40] = {0};
    message_send_direct(g_store, "alice", "bob", "Consume me", 0, id);

    int rc = message_consume(g_store, id);
    TEST_ASSERT_EQUAL(0, rc);

    PendingMessage* msg = message_peek_pending(g_store, "bob");
    TEST_ASSERT_NULL(msg);
}

void test_consume_then_peek_next(void) {
    char id1[40] = {0}, id2[40] = {0};
    message_send_direct(g_store, "alice", "bob", "First", 0, id1);
    message_send_direct(g_store, "alice", "bob", "Second", 0, id2);

    int rc = message_consume(g_store, id1);
    TEST_ASSERT_EQUAL(0, rc);

    PendingMessage* msg = message_peek_pending(g_store, "bob");
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL_STRING(id2, msg->id);
    TEST_ASSERT_EQUAL_STRING("Second", msg->content);
    pending_message_free(msg);
}

void test_consume_nonexistent(void) {
    int rc = message_consume(g_store, "no-such-id-at-all");
    TEST_ASSERT_EQUAL(-1, rc);
}

void test_consume_already_consumed(void) {
    char id[40] = {0};
    message_send_direct(g_store, "alice", "bob", "Once only", 0, id);

    int rc = message_consume(g_store, id);
    TEST_ASSERT_EQUAL(0, rc);

    rc = message_consume(g_store, id);
    TEST_ASSERT_EQUAL(-1, rc);
}

void test_channel_has_pending_no_subscriptions(void) {
    int pending = channel_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(0, pending);
}

void test_channel_has_pending_no_messages(void) {
    channel_create(g_store, "empty-channel", "Empty test", "creator", 0);
    channel_subscribe(g_store, "empty-channel", "agent-1");

    int pending = channel_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(0, pending);
}

void test_channel_has_pending_with_messages(void) {
    channel_create(g_store, "msg-channel", "Message test", "creator", 0);
    channel_subscribe(g_store, "msg-channel", "agent-1");

    char msg_id[40] = {0};
    channel_publish(g_store, "msg-channel", "publisher", "Test message", msg_id);

    int pending = channel_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(1, pending);
}

void test_channel_has_pending_after_read(void) {
    channel_create(g_store, "read-test-ch", "Read test", "creator", 0);
    channel_subscribe(g_store, "read-test-ch", "agent-1");

    char msg_id[40] = {0};
    channel_publish(g_store, "read-test-ch", "publisher", "Message to read", msg_id);

    size_t count = 0;
    ChannelMessage** msgs = channel_receive(g_store, "read-test-ch", "agent-1", 10, &count);
    TEST_ASSERT_EQUAL(1, count);
    channel_message_free_list(msgs, count);

    int pending = channel_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(0, pending);
}

void test_channel_has_pending_multiple_channels(void) {
    channel_create(g_store, "multi-ch-1", "Channel 1", "creator", 0);
    channel_create(g_store, "multi-ch-2", "Channel 2", "creator", 0);
    channel_subscribe(g_store, "multi-ch-1", "agent-1");
    channel_subscribe(g_store, "multi-ch-2", "agent-1");

    int pending = channel_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(0, pending);

    char msg_id[40] = {0};
    channel_publish(g_store, "multi-ch-2", "publisher", "Message on ch2", msg_id);

    pending = channel_has_pending(g_store, "agent-1");
    TEST_ASSERT_EQUAL(1, pending);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_message_store_create_destroy);
    RUN_TEST(test_message_store_multiple_instances);

    RUN_TEST(test_send_direct_message);
    RUN_TEST(test_send_direct_message_null_params);
    RUN_TEST(test_receive_direct_messages);
    RUN_TEST(test_receive_marks_as_read);
    RUN_TEST(test_message_has_pending);
    RUN_TEST(test_message_get_direct);
    RUN_TEST(test_send_message_with_ttl);

    RUN_TEST(test_channel_create);
    RUN_TEST(test_channel_create_persistent);
    RUN_TEST(test_channel_list);
    RUN_TEST(test_channel_delete);
    RUN_TEST(test_channel_subscribe);
    RUN_TEST(test_channel_unsubscribe);
    RUN_TEST(test_channel_get_subscribers);
    RUN_TEST(test_channel_get_agent_subscriptions);
    RUN_TEST(test_channel_publish);
    RUN_TEST(test_channel_receive);
    RUN_TEST(test_channel_receive_updates_last_read);
    RUN_TEST(test_channel_receive_all);

    RUN_TEST(test_message_cleanup_read);
    RUN_TEST(test_message_cleanup_expired);
    RUN_TEST(test_message_cleanup_agent);
    RUN_TEST(test_message_cleanup_channel_messages);

    RUN_TEST(test_cross_process_access);
    RUN_TEST(test_channel_delete_cascades);

    RUN_TEST(test_peek_pending_empty);
    RUN_TEST(test_peek_pending_returns_oldest);
    RUN_TEST(test_peek_pending_does_not_consume);
    RUN_TEST(test_consume_marks_as_read);
    RUN_TEST(test_consume_then_peek_next);
    RUN_TEST(test_consume_nonexistent);
    RUN_TEST(test_consume_already_consumed);

    RUN_TEST(test_channel_has_pending_no_subscriptions);
    RUN_TEST(test_channel_has_pending_no_messages);
    RUN_TEST(test_channel_has_pending_with_messages);
    RUN_TEST(test_channel_has_pending_after_read);
    RUN_TEST(test_channel_has_pending_multiple_channels);

    return UNITY_END();
}
