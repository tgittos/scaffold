#include "unity/unity.h"
#include "../../lib/agent/message_queue.h"
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

void test_create_returns_non_null(void) {
    message_queue_t* queue = message_queue_create();
    TEST_ASSERT_NOT_NULL(queue);
    message_queue_destroy(queue);
}

void test_new_queue_is_empty(void) {
    message_queue_t* queue = message_queue_create();
    TEST_ASSERT_EQUAL(0, message_queue_count(queue));
    message_queue_destroy(queue);
}

void test_count_null_queue_is_zero(void) {
    TEST_ASSERT_EQUAL(0, message_queue_count(NULL));
}

void test_pop_empty_queue_returns_null(void) {
    message_queue_t* queue = message_queue_create();
    char* result = message_queue_pop(queue);
    TEST_ASSERT_NULL(result);
    message_queue_destroy(queue);
}

void test_pop_null_queue_returns_null(void) {
    TEST_ASSERT_NULL(message_queue_pop(NULL));
}

void test_push_null_message_returns_error(void) {
    message_queue_t* queue = message_queue_create();
    TEST_ASSERT_EQUAL(-1, message_queue_push(queue, NULL));
    message_queue_destroy(queue);
}

void test_push_null_queue_returns_error(void) {
    TEST_ASSERT_EQUAL(-1, message_queue_push(NULL, "hello"));
}

void test_push_increments_count(void) {
    message_queue_t* queue = message_queue_create();
    message_queue_push(queue, "first");
    TEST_ASSERT_EQUAL(1, message_queue_count(queue));
    message_queue_push(queue, "second");
    TEST_ASSERT_EQUAL(2, message_queue_count(queue));
    message_queue_destroy(queue);
}

void test_push_then_pop_returns_copy(void) {
    message_queue_t* queue = message_queue_create();
    char original[] = "hello world";
    message_queue_push(queue, original);
    original[0] = 'X'; /* mutate source to prove push copied it */

    char* popped = message_queue_pop(queue);
    TEST_ASSERT_NOT_NULL(popped);
    TEST_ASSERT_EQUAL_STRING("hello world", popped);
    free(popped);
    message_queue_destroy(queue);
}

void test_fifo_order(void) {
    message_queue_t* queue = message_queue_create();
    message_queue_push(queue, "one");
    message_queue_push(queue, "two");
    message_queue_push(queue, "three");

    char* a = message_queue_pop(queue);
    char* b = message_queue_pop(queue);
    char* c = message_queue_pop(queue);

    TEST_ASSERT_EQUAL_STRING("one", a);
    TEST_ASSERT_EQUAL_STRING("two", b);
    TEST_ASSERT_EQUAL_STRING("three", c);

    free(a);
    free(b);
    free(c);
    message_queue_destroy(queue);
}

void test_pop_decrements_count(void) {
    message_queue_t* queue = message_queue_create();
    message_queue_push(queue, "one");
    message_queue_push(queue, "two");
    char* popped = message_queue_pop(queue);
    TEST_ASSERT_EQUAL(1, message_queue_count(queue));
    free(popped);
    message_queue_destroy(queue);
}

void test_pop_until_empty_then_null(void) {
    message_queue_t* queue = message_queue_create();
    message_queue_push(queue, "only");
    char* popped = message_queue_pop(queue);
    TEST_ASSERT_NOT_NULL(popped);
    free(popped);

    TEST_ASSERT_NULL(message_queue_pop(queue));
    TEST_ASSERT_EQUAL(0, message_queue_count(queue));
    message_queue_destroy(queue);
}

void test_destroy_frees_unpopped_messages(void) {
    /* Not directly observable, but exercises the free path under a leak
     * checker (valgrind / asan) without crashing. */
    message_queue_t* queue = message_queue_create();
    message_queue_push(queue, "leftover one");
    message_queue_push(queue, "leftover two");
    message_queue_destroy(queue);
    TEST_PASS();
}

void test_destroy_null_is_safe(void) {
    message_queue_destroy(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_returns_non_null);
    RUN_TEST(test_new_queue_is_empty);
    RUN_TEST(test_count_null_queue_is_zero);
    RUN_TEST(test_pop_empty_queue_returns_null);
    RUN_TEST(test_pop_null_queue_returns_null);
    RUN_TEST(test_push_null_message_returns_error);
    RUN_TEST(test_push_null_queue_returns_error);
    RUN_TEST(test_push_increments_count);
    RUN_TEST(test_push_then_pop_returns_copy);
    RUN_TEST(test_fifo_order);
    RUN_TEST(test_pop_decrements_count);
    RUN_TEST(test_pop_until_empty_then_null);
    RUN_TEST(test_destroy_frees_unpopped_messages);
    RUN_TEST(test_destroy_null_is_safe);

    return UNITY_END();
}
