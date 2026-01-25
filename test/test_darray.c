#include "unity/unity.h"
#include "../src/utils/darray.h"
#include <string.h>

/* Define test array types */
DARRAY_IMPL(IntArray, int)
DARRAY_IMPL(DoubleArray, double)

/* Define a struct for testing */
typedef struct {
    int id;
    char name[32];
} TestItem;

DARRAY_IMPL(TestItemArray, TestItem)

void setUp(void) {}
void tearDown(void) {}

/* Basic initialization tests */

void test_init_creates_empty_array(void) {
    IntArray arr = {0};
    TEST_ASSERT_EQUAL_INT(0, IntArray_init(&arr));
    TEST_ASSERT_NOT_NULL(arr.data);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_size_t(DARRAY_DEFAULT_CAPACITY, arr.capacity);
    IntArray_destroy(&arr);
}

void test_init_with_custom_capacity(void) {
    IntArray arr = {0};
    TEST_ASSERT_EQUAL_INT(0, IntArray_init_capacity(&arr, 100));
    TEST_ASSERT_NOT_NULL(arr.data);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_size_t(100, arr.capacity);
    IntArray_destroy(&arr);
}

void test_init_null_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(-1, IntArray_init(NULL));
}

void test_init_zero_capacity_uses_default(void) {
    IntArray arr = {0};
    TEST_ASSERT_EQUAL_INT(0, IntArray_init_capacity(&arr, 0));
    TEST_ASSERT_EQUAL_size_t(DARRAY_DEFAULT_CAPACITY, arr.capacity);
    IntArray_destroy(&arr);
}

/* Destroy tests */

void test_destroy_cleans_up(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);
    IntArray_destroy(&arr);
    TEST_ASSERT_NULL(arr.data);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_size_t(0, arr.capacity);
}

void test_destroy_null_is_safe(void) {
    IntArray_destroy(NULL);  /* Should not crash */
    TEST_PASS();
}

/* Push tests */

void test_push_adds_element(void) {
    IntArray arr = {0};
    IntArray_init(&arr);

    TEST_ASSERT_EQUAL_INT(0, IntArray_push(&arr, 42));
    TEST_ASSERT_EQUAL_size_t(1, arr.count);
    TEST_ASSERT_EQUAL_INT(42, arr.data[0]);

    IntArray_destroy(&arr);
}

void test_push_multiple_elements(void) {
    IntArray arr = {0};
    IntArray_init(&arr);

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, IntArray_push(&arr, i * 10));
    }

    TEST_ASSERT_EQUAL_size_t(10, arr.count);
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(i * 10, arr.data[i]);
    }

    IntArray_destroy(&arr);
}

void test_push_grows_capacity(void) {
    IntArray arr = {0};
    IntArray_init_capacity(&arr, 2);

    IntArray_push(&arr, 1);
    IntArray_push(&arr, 2);
    TEST_ASSERT_EQUAL_size_t(2, arr.capacity);

    IntArray_push(&arr, 3);  /* Should trigger growth */
    TEST_ASSERT_GREATER_THAN(2, arr.capacity);
    TEST_ASSERT_EQUAL_size_t(3, arr.count);
    TEST_ASSERT_EQUAL_INT(3, arr.data[2]);

    IntArray_destroy(&arr);
}

void test_push_null_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(-1, IntArray_push(NULL, 42));
}

/* Pop tests */

void test_pop_removes_last_element(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 20);
    IntArray_push(&arr, 30);

    int value = 0;
    TEST_ASSERT_EQUAL_INT(0, IntArray_pop(&arr, &value));
    TEST_ASSERT_EQUAL_INT(30, value);
    TEST_ASSERT_EQUAL_size_t(2, arr.count);

    IntArray_destroy(&arr);
}

void test_pop_with_null_out(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);

    TEST_ASSERT_EQUAL_INT(0, IntArray_pop(&arr, NULL));
    TEST_ASSERT_EQUAL_size_t(0, arr.count);

    IntArray_destroy(&arr);
}

void test_pop_empty_returns_error(void) {
    IntArray arr = {0};
    IntArray_init(&arr);

    int value = 0;
    TEST_ASSERT_EQUAL_INT(-1, IntArray_pop(&arr, &value));

    IntArray_destroy(&arr);
}

/* Get tests */

void test_get_returns_pointer_to_element(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);

    int* ptr = IntArray_get(&arr, 0);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_INT(42, *ptr);

    IntArray_destroy(&arr);
}

void test_get_out_of_bounds_returns_null(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);

    TEST_ASSERT_NULL(IntArray_get(&arr, 1));
    TEST_ASSERT_NULL(IntArray_get(&arr, 100));

    IntArray_destroy(&arr);
}

void test_get_null_returns_null(void) {
    TEST_ASSERT_NULL(IntArray_get(NULL, 0));
}

/* Set tests */

void test_set_modifies_element(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 20);

    TEST_ASSERT_EQUAL_INT(0, IntArray_set(&arr, 1, 200));
    TEST_ASSERT_EQUAL_INT(200, arr.data[1]);

    IntArray_destroy(&arr);
}

void test_set_out_of_bounds_returns_error(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);

    TEST_ASSERT_EQUAL_INT(-1, IntArray_set(&arr, 1, 100));

    IntArray_destroy(&arr);
}

/* Insert tests */

void test_insert_at_beginning(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 20);
    IntArray_push(&arr, 30);

    TEST_ASSERT_EQUAL_INT(0, IntArray_insert(&arr, 0, 10));
    TEST_ASSERT_EQUAL_size_t(3, arr.count);
    TEST_ASSERT_EQUAL_INT(10, arr.data[0]);
    TEST_ASSERT_EQUAL_INT(20, arr.data[1]);
    TEST_ASSERT_EQUAL_INT(30, arr.data[2]);

    IntArray_destroy(&arr);
}

void test_insert_in_middle(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 30);

    TEST_ASSERT_EQUAL_INT(0, IntArray_insert(&arr, 1, 20));
    TEST_ASSERT_EQUAL_size_t(3, arr.count);
    TEST_ASSERT_EQUAL_INT(10, arr.data[0]);
    TEST_ASSERT_EQUAL_INT(20, arr.data[1]);
    TEST_ASSERT_EQUAL_INT(30, arr.data[2]);

    IntArray_destroy(&arr);
}

void test_insert_at_end(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 20);

    TEST_ASSERT_EQUAL_INT(0, IntArray_insert(&arr, 2, 30));
    TEST_ASSERT_EQUAL_size_t(3, arr.count);
    TEST_ASSERT_EQUAL_INT(30, arr.data[2]);

    IntArray_destroy(&arr);
}

void test_insert_grows_capacity(void) {
    IntArray arr = {0};
    IntArray_init_capacity(&arr, 2);
    IntArray_push(&arr, 1);
    IntArray_push(&arr, 2);

    TEST_ASSERT_EQUAL_INT(0, IntArray_insert(&arr, 1, 100));
    TEST_ASSERT_GREATER_THAN(2, arr.capacity);

    IntArray_destroy(&arr);
}

void test_insert_out_of_bounds_returns_error(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);

    TEST_ASSERT_EQUAL_INT(-1, IntArray_insert(&arr, 5, 100));

    IntArray_destroy(&arr);
}

/* Remove tests */

void test_remove_from_beginning(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 20);
    IntArray_push(&arr, 30);

    TEST_ASSERT_EQUAL_INT(0, IntArray_remove(&arr, 0));
    TEST_ASSERT_EQUAL_size_t(2, arr.count);
    TEST_ASSERT_EQUAL_INT(20, arr.data[0]);
    TEST_ASSERT_EQUAL_INT(30, arr.data[1]);

    IntArray_destroy(&arr);
}

void test_remove_from_middle(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 20);
    IntArray_push(&arr, 30);

    TEST_ASSERT_EQUAL_INT(0, IntArray_remove(&arr, 1));
    TEST_ASSERT_EQUAL_size_t(2, arr.count);
    TEST_ASSERT_EQUAL_INT(10, arr.data[0]);
    TEST_ASSERT_EQUAL_INT(30, arr.data[1]);

    IntArray_destroy(&arr);
}

void test_remove_from_end(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 10);
    IntArray_push(&arr, 20);
    IntArray_push(&arr, 30);

    TEST_ASSERT_EQUAL_INT(0, IntArray_remove(&arr, 2));
    TEST_ASSERT_EQUAL_size_t(2, arr.count);
    TEST_ASSERT_EQUAL_INT(10, arr.data[0]);
    TEST_ASSERT_EQUAL_INT(20, arr.data[1]);

    IntArray_destroy(&arr);
}

void test_remove_out_of_bounds_returns_error(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 42);

    TEST_ASSERT_EQUAL_INT(-1, IntArray_remove(&arr, 1));

    IntArray_destroy(&arr);
}

/* Clear tests */

void test_clear_resets_count(void) {
    IntArray arr = {0};
    IntArray_init(&arr);
    IntArray_push(&arr, 1);
    IntArray_push(&arr, 2);
    IntArray_push(&arr, 3);

    IntArray_clear(&arr);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_NOT_NULL(arr.data);  /* Data buffer preserved */

    IntArray_destroy(&arr);
}

void test_clear_null_is_safe(void) {
    IntArray_clear(NULL);  /* Should not crash */
    TEST_PASS();
}

/* Shrink tests */

void test_shrink_reduces_capacity(void) {
    IntArray arr = {0};
    IntArray_init_capacity(&arr, 100);
    IntArray_push(&arr, 1);
    IntArray_push(&arr, 2);
    IntArray_push(&arr, 3);

    TEST_ASSERT_EQUAL_INT(0, IntArray_shrink(&arr));
    TEST_ASSERT_EQUAL_size_t(3, arr.capacity);
    TEST_ASSERT_EQUAL_size_t(3, arr.count);

    IntArray_destroy(&arr);
}

void test_shrink_empty_returns_error(void) {
    IntArray arr = {0};
    IntArray_init(&arr);

    TEST_ASSERT_EQUAL_INT(-1, IntArray_shrink(&arr));

    IntArray_destroy(&arr);
}

void test_shrink_already_minimal_is_noop(void) {
    IntArray arr = {0};
    IntArray_init_capacity(&arr, 2);
    IntArray_push(&arr, 1);
    IntArray_push(&arr, 2);

    TEST_ASSERT_EQUAL_INT(0, IntArray_shrink(&arr));
    TEST_ASSERT_EQUAL_size_t(2, arr.capacity);

    IntArray_destroy(&arr);
}

/* Reserve tests */

void test_reserve_increases_capacity(void) {
    IntArray arr = {0};
    IntArray_init(&arr);

    TEST_ASSERT_EQUAL_INT(0, IntArray_reserve(&arr, 100));
    TEST_ASSERT_EQUAL_size_t(100, arr.capacity);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);

    IntArray_destroy(&arr);
}

void test_reserve_smaller_is_noop(void) {
    IntArray arr = {0};
    IntArray_init_capacity(&arr, 100);

    TEST_ASSERT_EQUAL_INT(0, IntArray_reserve(&arr, 50));
    TEST_ASSERT_EQUAL_size_t(100, arr.capacity);

    IntArray_destroy(&arr);
}

/* Struct array tests */

void test_struct_array(void) {
    TestItemArray arr = {0};
    TestItemArray_init(&arr);

    TestItem item1 = { .id = 1 };
    strncpy(item1.name, "First", sizeof(item1.name));

    TestItem item2 = { .id = 2 };
    strncpy(item2.name, "Second", sizeof(item2.name));

    TestItemArray_push(&arr, item1);
    TestItemArray_push(&arr, item2);

    TEST_ASSERT_EQUAL_size_t(2, arr.count);

    TestItem* retrieved = TestItemArray_get(&arr, 0);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_EQUAL_INT(1, retrieved->id);
    TEST_ASSERT_EQUAL_STRING("First", retrieved->name);

    retrieved = TestItemArray_get(&arr, 1);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_EQUAL_INT(2, retrieved->id);
    TEST_ASSERT_EQUAL_STRING("Second", retrieved->name);

    TestItemArray_destroy(&arr);
}

/* Double array tests (floating point) */

void test_double_array(void) {
    DoubleArray arr = {0};
    DoubleArray_init(&arr);

    DoubleArray_push(&arr, 3.14159);
    DoubleArray_push(&arr, 2.71828);

    TEST_ASSERT_EQUAL_size_t(2, arr.count);
    /* Use simple comparison since Unity double precision may be disabled */
    TEST_ASSERT_TRUE(arr.data[0] > 3.14 && arr.data[0] < 3.15);
    TEST_ASSERT_TRUE(arr.data[1] > 2.71 && arr.data[1] < 2.72);

    DoubleArray_destroy(&arr);
}

/* Stress test */

void test_large_number_of_elements(void) {
    IntArray arr = {0};
    IntArray_init(&arr);

    const int N = 10000;
    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(0, IntArray_push(&arr, i));
    }

    TEST_ASSERT_EQUAL_size_t(N, arr.count);
    TEST_ASSERT_TRUE(arr.capacity >= (size_t)N);

    /* Verify all elements */
    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(i, arr.data[i]);
    }

    IntArray_destroy(&arr);
}

int main(void) {
    UNITY_BEGIN();

    /* Initialization tests */
    RUN_TEST(test_init_creates_empty_array);
    RUN_TEST(test_init_with_custom_capacity);
    RUN_TEST(test_init_null_returns_error);
    RUN_TEST(test_init_zero_capacity_uses_default);

    /* Destroy tests */
    RUN_TEST(test_destroy_cleans_up);
    RUN_TEST(test_destroy_null_is_safe);

    /* Push tests */
    RUN_TEST(test_push_adds_element);
    RUN_TEST(test_push_multiple_elements);
    RUN_TEST(test_push_grows_capacity);
    RUN_TEST(test_push_null_returns_error);

    /* Pop tests */
    RUN_TEST(test_pop_removes_last_element);
    RUN_TEST(test_pop_with_null_out);
    RUN_TEST(test_pop_empty_returns_error);

    /* Get tests */
    RUN_TEST(test_get_returns_pointer_to_element);
    RUN_TEST(test_get_out_of_bounds_returns_null);
    RUN_TEST(test_get_null_returns_null);

    /* Set tests */
    RUN_TEST(test_set_modifies_element);
    RUN_TEST(test_set_out_of_bounds_returns_error);

    /* Insert tests */
    RUN_TEST(test_insert_at_beginning);
    RUN_TEST(test_insert_in_middle);
    RUN_TEST(test_insert_at_end);
    RUN_TEST(test_insert_grows_capacity);
    RUN_TEST(test_insert_out_of_bounds_returns_error);

    /* Remove tests */
    RUN_TEST(test_remove_from_beginning);
    RUN_TEST(test_remove_from_middle);
    RUN_TEST(test_remove_from_end);
    RUN_TEST(test_remove_out_of_bounds_returns_error);

    /* Clear tests */
    RUN_TEST(test_clear_resets_count);
    RUN_TEST(test_clear_null_is_safe);

    /* Shrink tests */
    RUN_TEST(test_shrink_reduces_capacity);
    RUN_TEST(test_shrink_empty_returns_error);
    RUN_TEST(test_shrink_already_minimal_is_noop);

    /* Reserve tests */
    RUN_TEST(test_reserve_increases_capacity);
    RUN_TEST(test_reserve_smaller_is_noop);

    /* Struct and double tests */
    RUN_TEST(test_struct_array);
    RUN_TEST(test_double_array);

    /* Stress test */
    RUN_TEST(test_large_number_of_elements);

    return UNITY_END();
}
