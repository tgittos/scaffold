#include "unity/unity.h"
#include "../../src/core/async_executor.h"
#include "../../src/core/interrupt.h"
#include <unistd.h>
#include <fcntl.h>

void setUp(void) {
    interrupt_cleanup();
}

void tearDown(void) {
    interrupt_cleanup();
}

void test_async_executor_create_null_session_returns_null(void) {
    async_executor_t* executor = async_executor_create(NULL);
    TEST_ASSERT_NULL(executor);
}

void test_async_executor_create_returns_valid_executor(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);
    async_executor_destroy(executor);
}

void test_async_executor_destroy_null_is_safe(void) {
    async_executor_destroy(NULL);
    TEST_PASS();
}

void test_async_executor_get_notify_fd_returns_valid_fd(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    int fd = async_executor_get_notify_fd(executor);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    int flags = fcntl(fd, F_GETFL);
    TEST_ASSERT_NOT_EQUAL(-1, flags);
    TEST_ASSERT_TRUE(flags & O_NONBLOCK);

    async_executor_destroy(executor);
}

void test_async_executor_get_notify_fd_null_returns_neg_one(void) {
    int fd = async_executor_get_notify_fd(NULL);
    TEST_ASSERT_EQUAL(-1, fd);
}

void test_async_executor_is_running_initially_false(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    TEST_ASSERT_EQUAL(0, async_executor_is_running(executor));

    async_executor_destroy(executor);
}

void test_async_executor_is_running_null_returns_false(void) {
    TEST_ASSERT_EQUAL(0, async_executor_is_running(NULL));
}

void test_async_executor_start_null_executor_returns_error(void) {
    int result = async_executor_start(NULL, "test message");
    TEST_ASSERT_EQUAL(-1, result);
}

void test_async_executor_start_null_message_returns_error(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    int result = async_executor_start(executor, NULL);
    TEST_ASSERT_EQUAL(-1, result);

    async_executor_destroy(executor);
}

void test_async_executor_cancel_null_is_safe(void) {
    async_executor_cancel(NULL);
    TEST_PASS();
}

void test_async_executor_cancel_when_not_running_is_safe(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    async_executor_cancel(executor);
    TEST_PASS();

    async_executor_destroy(executor);
}

void test_async_executor_wait_null_returns_error(void) {
    int result = async_executor_wait(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_async_executor_get_error_null_returns_null(void) {
    const char* error = async_executor_get_error(NULL);
    TEST_ASSERT_NULL(error);
}

void test_async_executor_get_result_null_returns_error(void) {
    int result = async_executor_get_result(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_async_executor_get_result_initial_is_zero(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    int result = async_executor_get_result(executor);
    TEST_ASSERT_EQUAL(0, result);

    async_executor_destroy(executor);
}

void test_async_executor_process_events_null_returns_error(void) {
    int result = async_executor_process_events(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_async_executor_process_events_no_pending_returns_zero(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    int result = async_executor_process_events(executor);
    TEST_ASSERT_EQUAL(0, result);

    async_executor_destroy(executor);
}

void test_interrupt_handler_trigger_sets_flag(void) {
    interrupt_init();
    TEST_ASSERT_EQUAL(0, interrupt_pending());

    interrupt_handler_trigger();
    TEST_ASSERT_EQUAL(1, interrupt_pending());
}

void test_async_executor_get_active_null_before_creation(void) {
    /* Get active should return NULL when no executor exists */
    async_executor_t* active = async_executor_get_active();
    TEST_ASSERT_NULL(active);
}

void test_async_executor_get_active_returns_executor_after_creation(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    async_executor_t* active = async_executor_get_active();
    TEST_ASSERT_EQUAL_PTR(executor, active);

    async_executor_destroy(executor);
}

void test_async_executor_get_active_null_after_destruction(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    async_executor_destroy(executor);

    async_executor_t* active = async_executor_get_active();
    TEST_ASSERT_NULL(active);
}

void test_async_executor_notify_subagent_spawned_null_is_safe(void) {
    /* Calling notify on NULL executor should be a no-op */
    async_executor_notify_subagent_spawned(NULL);
    TEST_PASS();
}

void test_async_executor_notify_subagent_spawned_when_not_running_is_noop(void) {
    RalphSession session = {0};
    async_executor_t* executor = async_executor_create(&session);
    TEST_ASSERT_NOT_NULL(executor);

    /* When not running, notify should not send any event */
    async_executor_notify_subagent_spawned(executor);

    /* Process events should return 0 (no event) since notify was a no-op */
    int result = async_executor_process_events(executor);
    TEST_ASSERT_EQUAL(0, result);

    async_executor_destroy(executor);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_async_executor_create_null_session_returns_null);
    RUN_TEST(test_async_executor_create_returns_valid_executor);
    RUN_TEST(test_async_executor_destroy_null_is_safe);
    RUN_TEST(test_async_executor_get_notify_fd_returns_valid_fd);
    RUN_TEST(test_async_executor_get_notify_fd_null_returns_neg_one);
    RUN_TEST(test_async_executor_is_running_initially_false);
    RUN_TEST(test_async_executor_is_running_null_returns_false);
    RUN_TEST(test_async_executor_start_null_executor_returns_error);
    RUN_TEST(test_async_executor_start_null_message_returns_error);
    RUN_TEST(test_async_executor_cancel_null_is_safe);
    RUN_TEST(test_async_executor_cancel_when_not_running_is_safe);
    RUN_TEST(test_async_executor_wait_null_returns_error);
    RUN_TEST(test_async_executor_get_error_null_returns_null);
    RUN_TEST(test_async_executor_get_result_null_returns_error);
    RUN_TEST(test_async_executor_get_result_initial_is_zero);
    RUN_TEST(test_async_executor_process_events_null_returns_error);
    RUN_TEST(test_async_executor_process_events_no_pending_returns_zero);
    RUN_TEST(test_interrupt_handler_trigger_sets_flag);
    RUN_TEST(test_async_executor_get_active_null_before_creation);
    RUN_TEST(test_async_executor_get_active_returns_executor_after_creation);
    RUN_TEST(test_async_executor_get_active_null_after_destruction);
    RUN_TEST(test_async_executor_notify_subagent_spawned_null_is_safe);
    RUN_TEST(test_async_executor_notify_subagent_spawned_when_not_running_is_noop);

    return UNITY_END();
}
