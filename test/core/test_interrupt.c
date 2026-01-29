#include "unity/unity.h"
#include "../../src/core/interrupt.h"
#include <signal.h>
#include <stdlib.h>

void setUp(void) {
    interrupt_cleanup();
}

void tearDown(void) {
    interrupt_cleanup();
}

void test_interrupt_init_returns_success(void) {
    int result = interrupt_init();
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_interrupt_init_twice_is_idempotent(void) {
    int result1 = interrupt_init();
    int result2 = interrupt_init();
    TEST_ASSERT_EQUAL_INT(0, result1);
    TEST_ASSERT_EQUAL_INT(0, result2);
}

void test_interrupt_pending_initially_false(void) {
    interrupt_init();
    TEST_ASSERT_EQUAL_INT(0, interrupt_pending());
}

void test_interrupt_pending_after_signal(void) {
    interrupt_init();
    raise(SIGINT);
    TEST_ASSERT_EQUAL_INT(1, interrupt_pending());
}

void test_interrupt_clear_resets_flag(void) {
    interrupt_init();
    raise(SIGINT);
    TEST_ASSERT_EQUAL_INT(1, interrupt_pending());
    interrupt_clear();
    TEST_ASSERT_EQUAL_INT(0, interrupt_pending());
}

void test_interrupt_acknowledge_suppresses_pending(void) {
    interrupt_init();
    raise(SIGINT);
    TEST_ASSERT_EQUAL_INT(1, interrupt_pending());
    interrupt_acknowledge();
    TEST_ASSERT_EQUAL_INT(0, interrupt_pending());
}

void test_interrupt_clear_also_clears_acknowledge(void) {
    interrupt_init();
    raise(SIGINT);
    interrupt_acknowledge();
    TEST_ASSERT_EQUAL_INT(0, interrupt_pending());
    interrupt_clear();
    raise(SIGINT);
    TEST_ASSERT_EQUAL_INT(1, interrupt_pending());
}

void test_interrupt_cleanup_without_init(void) {
    interrupt_cleanup();
    TEST_PASS();
}

void test_interrupt_cleanup_twice(void) {
    interrupt_init();
    interrupt_cleanup();
    interrupt_cleanup();
    TEST_PASS();
}

void test_interrupt_pending_after_cleanup(void) {
    interrupt_init();
    raise(SIGINT);
    interrupt_cleanup();
    TEST_ASSERT_EQUAL_INT(0, interrupt_pending());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_interrupt_init_returns_success);
    RUN_TEST(test_interrupt_init_twice_is_idempotent);
    RUN_TEST(test_interrupt_pending_initially_false);
    RUN_TEST(test_interrupt_pending_after_signal);
    RUN_TEST(test_interrupt_clear_resets_flag);
    RUN_TEST(test_interrupt_acknowledge_suppresses_pending);
    RUN_TEST(test_interrupt_clear_also_clears_acknowledge);
    RUN_TEST(test_interrupt_cleanup_without_init);
    RUN_TEST(test_interrupt_cleanup_twice);
    RUN_TEST(test_interrupt_pending_after_cleanup);

    return UNITY_END();
}
