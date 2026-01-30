#include "unity/unity.h"
#include "../../src/utils/pipe_notifier.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

void test_init_creates_valid_fds(void) {
    PipeNotifier notifier = {0};
    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_init(&notifier));
    TEST_ASSERT_TRUE(notifier.read_fd >= 0);
    TEST_ASSERT_TRUE(notifier.write_fd >= 0);
    TEST_ASSERT_NOT_EQUAL(notifier.read_fd, notifier.write_fd);
    pipe_notifier_destroy(&notifier);
}

void test_init_null_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(-1, pipe_notifier_init(NULL));
}

void test_destroy_closes_fds(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);
    int read_fd = notifier.read_fd;
    int write_fd = notifier.write_fd;

    pipe_notifier_destroy(&notifier);

    TEST_ASSERT_EQUAL_INT(-1, notifier.read_fd);
    TEST_ASSERT_EQUAL_INT(-1, notifier.write_fd);

    /* Verify fds are actually closed by checking they're invalid */
    TEST_ASSERT_EQUAL_INT(-1, fcntl(read_fd, F_GETFD));
    TEST_ASSERT_EQUAL_INT(-1, fcntl(write_fd, F_GETFD));
}

void test_destroy_null_is_safe(void) {
    pipe_notifier_destroy(NULL);
    TEST_PASS();
}

void test_send_recv_single_event(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_send(&notifier, 'A'));

    char event = 0;
    TEST_ASSERT_EQUAL_INT(1, pipe_notifier_recv(&notifier, &event));
    TEST_ASSERT_EQUAL_CHAR('A', event);

    pipe_notifier_destroy(&notifier);
}

void test_send_recv_multiple_events(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_send(&notifier, 'X'));
    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_send(&notifier, 'Y'));
    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_send(&notifier, 'Z'));

    char event = 0;
    TEST_ASSERT_EQUAL_INT(1, pipe_notifier_recv(&notifier, &event));
    TEST_ASSERT_EQUAL_CHAR('X', event);

    TEST_ASSERT_EQUAL_INT(1, pipe_notifier_recv(&notifier, &event));
    TEST_ASSERT_EQUAL_CHAR('Y', event);

    TEST_ASSERT_EQUAL_INT(1, pipe_notifier_recv(&notifier, &event));
    TEST_ASSERT_EQUAL_CHAR('Z', event);

    pipe_notifier_destroy(&notifier);
}

void test_recv_no_data_returns_zero(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    char event = 0;
    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_recv(&notifier, &event));

    pipe_notifier_destroy(&notifier);
}

void test_send_null_notifier_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(-1, pipe_notifier_send(NULL, 'A'));
}

void test_recv_null_notifier_returns_error(void) {
    char event;
    TEST_ASSERT_EQUAL_INT(-1, pipe_notifier_recv(NULL, &event));
}

void test_recv_null_event_returns_error(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    TEST_ASSERT_EQUAL_INT(-1, pipe_notifier_recv(&notifier, NULL));

    pipe_notifier_destroy(&notifier);
}

void test_get_read_fd_returns_valid_fd(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    TEST_ASSERT_EQUAL_INT(notifier.read_fd, pipe_notifier_get_read_fd(&notifier));

    pipe_notifier_destroy(&notifier);
}

void test_get_read_fd_null_returns_negative(void) {
    TEST_ASSERT_EQUAL_INT(-1, pipe_notifier_get_read_fd(NULL));
}

void test_drain_clears_all_pending(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    /* Send multiple events */
    pipe_notifier_send(&notifier, '1');
    pipe_notifier_send(&notifier, '2');
    pipe_notifier_send(&notifier, '3');

    /* Drain all */
    pipe_notifier_drain(&notifier);

    /* Verify nothing remains */
    char event;
    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_recv(&notifier, &event));

    pipe_notifier_destroy(&notifier);
}

void test_drain_null_is_safe(void) {
    pipe_notifier_drain(NULL);
    TEST_PASS();
}

void test_drain_empty_notifier_is_safe(void) {
    PipeNotifier notifier = {0};
    pipe_notifier_init(&notifier);

    pipe_notifier_drain(&notifier);

    char event;
    TEST_ASSERT_EQUAL_INT(0, pipe_notifier_recv(&notifier, &event));

    pipe_notifier_destroy(&notifier);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_creates_valid_fds);
    RUN_TEST(test_init_null_returns_error);
    RUN_TEST(test_destroy_closes_fds);
    RUN_TEST(test_destroy_null_is_safe);
    RUN_TEST(test_send_recv_single_event);
    RUN_TEST(test_send_recv_multiple_events);
    RUN_TEST(test_recv_no_data_returns_zero);
    RUN_TEST(test_send_null_notifier_returns_error);
    RUN_TEST(test_recv_null_notifier_returns_error);
    RUN_TEST(test_recv_null_event_returns_error);
    RUN_TEST(test_get_read_fd_returns_valid_fd);
    RUN_TEST(test_get_read_fd_null_returns_negative);
    RUN_TEST(test_drain_clears_all_pending);
    RUN_TEST(test_drain_null_is_safe);
    RUN_TEST(test_drain_empty_notifier_is_safe);

    return UNITY_END();
}
