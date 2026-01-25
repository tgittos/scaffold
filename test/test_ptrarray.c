#include "unity/unity.h"
#include "../src/utils/ptrarray.h"
#include <string.h>

/* Test item structure */
typedef struct TestObj {
    int id;
    char* name;
} TestObj;

static int destructor_call_count = 0;

static void test_obj_free(TestObj* obj) {
    if (obj == NULL) return;
    destructor_call_count++;
    free(obj->name);
    free(obj);
}

static TestObj* test_obj_create(int id, const char* name) {
    TestObj* obj = malloc(sizeof(TestObj));
    if (obj == NULL) return NULL;
    obj->id = id;
    obj->name = name ? strdup(name) : NULL;
    return obj;
}

/* Declare pointer array for TestObj */
PTRARRAY_IMPL(TestObjArray, TestObj)

void setUp(void) {
    destructor_call_count = 0;
}

void tearDown(void) {}

/* Basic initialization tests */

void test_init_creates_empty_array(void) {
    TestObjArray arr = {0};
    TEST_ASSERT_EQUAL_INT(0, TestObjArray_init(&arr, NULL));
    TEST_ASSERT_NOT_NULL(arr.data);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_size_t(PTRARRAY_DEFAULT_CAPACITY, arr.capacity);
    TEST_ASSERT_NULL(arr.destructor);
    TestObjArray_destroy(&arr);
}

void test_init_with_destructor(void) {
    TestObjArray arr = {0};
    TEST_ASSERT_EQUAL_INT(0, TestObjArray_init(&arr, test_obj_free));
    TEST_ASSERT_NOT_NULL(arr.destructor);
    TestObjArray_destroy(&arr);
}

void test_init_with_custom_capacity(void) {
    TestObjArray arr = {0};
    TEST_ASSERT_EQUAL_INT(0, TestObjArray_init_capacity(&arr, 50, NULL));
    TEST_ASSERT_EQUAL_size_t(50, arr.capacity);
    TestObjArray_destroy(&arr);
}

void test_init_null_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(-1, TestObjArray_init(NULL, NULL));
}

/* Destroy tests */

void test_destroy_calls_destructor(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObjArray_push(&arr, test_obj_create(1, "one"));
    TestObjArray_push(&arr, test_obj_create(2, "two"));
    TestObjArray_push(&arr, test_obj_create(3, "three"));

    TestObjArray_destroy(&arr);
    TEST_ASSERT_EQUAL_INT(3, destructor_call_count);
    TEST_ASSERT_NULL(arr.data);
}

void test_destroy_shallow_does_not_call_destructor(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "one");
    TestObj* obj2 = test_obj_create(2, "two");
    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);

    TestObjArray_destroy_shallow(&arr);
    TEST_ASSERT_EQUAL_INT(0, destructor_call_count);
    TEST_ASSERT_NULL(arr.data);

    /* Clean up manually */
    test_obj_free(obj1);
    test_obj_free(obj2);
}

void test_destroy_null_is_safe(void) {
    TestObjArray_destroy(NULL);
    TEST_PASS();
}

/* Push tests */

void test_push_adds_element(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj = test_obj_create(42, "answer");
    TEST_ASSERT_EQUAL_INT(0, TestObjArray_push(&arr, obj));
    TEST_ASSERT_EQUAL_size_t(1, arr.count);
    TEST_ASSERT_EQUAL_PTR(obj, arr.data[0]);

    TestObjArray_destroy(&arr);
}

void test_push_grows_capacity(void) {
    TestObjArray arr = {0};
    TestObjArray_init_capacity(&arr, 2, test_obj_free);

    TestObjArray_push(&arr, test_obj_create(1, "a"));
    TestObjArray_push(&arr, test_obj_create(2, "b"));
    TEST_ASSERT_EQUAL_size_t(2, arr.capacity);

    TestObjArray_push(&arr, test_obj_create(3, "c"));
    TEST_ASSERT_GREATER_THAN(2, arr.capacity);

    TestObjArray_destroy(&arr);
}

void test_push_null_array_returns_error(void) {
    TestObj* obj = test_obj_create(1, "test");
    TEST_ASSERT_EQUAL_INT(-1, TestObjArray_push(NULL, obj));
    test_obj_free(obj);
}

/* Pop tests */

void test_pop_returns_last_element(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "first");
    TestObj* obj2 = test_obj_create(2, "second");
    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);

    TestObj* popped = TestObjArray_pop(&arr);
    TEST_ASSERT_EQUAL_PTR(obj2, popped);
    TEST_ASSERT_EQUAL_size_t(1, arr.count);

    /* Pop doesn't call destructor, caller owns the pointer */
    TEST_ASSERT_EQUAL_INT(0, destructor_call_count);

    test_obj_free(popped);
    TestObjArray_destroy(&arr);
}

void test_pop_empty_returns_null(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, NULL);

    TEST_ASSERT_NULL(TestObjArray_pop(&arr));

    TestObjArray_destroy(&arr);
}

/* Get tests */

void test_get_returns_element(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj = test_obj_create(42, "test");
    TestObjArray_push(&arr, obj);

    TEST_ASSERT_EQUAL_PTR(obj, TestObjArray_get(&arr, 0));

    TestObjArray_destroy(&arr);
}

void test_get_out_of_bounds_returns_null(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, NULL);

    TEST_ASSERT_NULL(TestObjArray_get(&arr, 0));
    TEST_ASSERT_NULL(TestObjArray_get(&arr, 100));

    TestObjArray_destroy(&arr);
}

void test_get_const_correctness(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);
    TestObjArray_push(&arr, test_obj_create(1, "test"));

    const TestObjArray* const_arr = &arr;
    TestObj* obj = TestObjArray_get(const_arr, 0);
    TEST_ASSERT_NOT_NULL(obj);
    TEST_ASSERT_EQUAL_INT(1, obj->id);

    TestObjArray_destroy(&arr);
}

/* Set tests */

void test_set_replaces_element_and_calls_destructor(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* old = test_obj_create(1, "old");
    TestObj* new_obj = test_obj_create(2, "new");
    TestObjArray_push(&arr, old);

    TEST_ASSERT_EQUAL_INT(0, TestObjArray_set(&arr, 0, new_obj));
    TEST_ASSERT_EQUAL_INT(1, destructor_call_count);  /* old was freed */
    TEST_ASSERT_EQUAL_PTR(new_obj, arr.data[0]);

    TestObjArray_destroy(&arr);
}

void test_set_without_destructor_does_not_free(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, NULL);  /* No destructor */

    TestObj* old = test_obj_create(1, "old");
    TestObj* new_obj = test_obj_create(2, "new");
    TestObjArray_push(&arr, old);

    TEST_ASSERT_EQUAL_INT(0, TestObjArray_set(&arr, 0, new_obj));
    TEST_ASSERT_EQUAL_INT(0, destructor_call_count);

    test_obj_free(old);  /* Manual cleanup */
    test_obj_free(new_obj);
    TestObjArray_destroy_shallow(&arr);
}

/* Insert tests */

void test_insert_at_beginning(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "first");
    TestObj* obj2 = test_obj_create(2, "second");
    TestObj* obj0 = test_obj_create(0, "zero");

    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);
    TestObjArray_insert(&arr, 0, obj0);

    TEST_ASSERT_EQUAL_size_t(3, arr.count);
    TEST_ASSERT_EQUAL_PTR(obj0, arr.data[0]);
    TEST_ASSERT_EQUAL_PTR(obj1, arr.data[1]);
    TEST_ASSERT_EQUAL_PTR(obj2, arr.data[2]);

    TestObjArray_destroy(&arr);
}

void test_insert_in_middle(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj0 = test_obj_create(0, "zero");
    TestObj* obj2 = test_obj_create(2, "two");
    TestObj* obj1 = test_obj_create(1, "one");

    TestObjArray_push(&arr, obj0);
    TestObjArray_push(&arr, obj2);
    TestObjArray_insert(&arr, 1, obj1);

    TEST_ASSERT_EQUAL_size_t(3, arr.count);
    TEST_ASSERT_EQUAL_PTR(obj0, arr.data[0]);
    TEST_ASSERT_EQUAL_PTR(obj1, arr.data[1]);
    TEST_ASSERT_EQUAL_PTR(obj2, arr.data[2]);

    TestObjArray_destroy(&arr);
}

/* Remove tests */

void test_remove_returns_element_without_freeing(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "first");
    TestObj* obj2 = test_obj_create(2, "second");
    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);

    TestObj* removed = TestObjArray_remove(&arr, 0);
    TEST_ASSERT_EQUAL_PTR(obj1, removed);
    TEST_ASSERT_EQUAL_size_t(1, arr.count);
    TEST_ASSERT_EQUAL_INT(0, destructor_call_count);

    test_obj_free(removed);
    TestObjArray_destroy(&arr);
}

void test_remove_out_of_bounds_returns_null(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, NULL);

    TEST_ASSERT_NULL(TestObjArray_remove(&arr, 0));

    TestObjArray_destroy(&arr);
}

/* Delete tests */

void test_delete_removes_and_frees(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObjArray_push(&arr, test_obj_create(1, "first"));
    TestObjArray_push(&arr, test_obj_create(2, "second"));

    TestObjArray_delete(&arr, 0);
    TEST_ASSERT_EQUAL_size_t(1, arr.count);
    TEST_ASSERT_EQUAL_INT(1, destructor_call_count);
    TEST_ASSERT_EQUAL_INT(2, arr.data[0]->id);  /* second is now first */

    TestObjArray_destroy(&arr);
}

/* Clear tests */

void test_clear_frees_all_elements(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObjArray_push(&arr, test_obj_create(1, "a"));
    TestObjArray_push(&arr, test_obj_create(2, "b"));
    TestObjArray_push(&arr, test_obj_create(3, "c"));

    TestObjArray_clear(&arr);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_INT(3, destructor_call_count);
    TEST_ASSERT_NOT_NULL(arr.data);  /* Buffer preserved */

    TestObjArray_destroy(&arr);
}

void test_clear_shallow_does_not_free(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "a");
    TestObj* obj2 = test_obj_create(2, "b");
    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);

    TestObjArray_clear_shallow(&arr);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_INT(0, destructor_call_count);

    /* Manual cleanup */
    test_obj_free(obj1);
    test_obj_free(obj2);
    TestObjArray_destroy(&arr);
}

/* Shrink tests */

void test_shrink_reduces_capacity(void) {
    TestObjArray arr = {0};
    TestObjArray_init_capacity(&arr, 100, test_obj_free);

    TestObjArray_push(&arr, test_obj_create(1, "a"));
    TestObjArray_push(&arr, test_obj_create(2, "b"));

    TEST_ASSERT_EQUAL_INT(0, TestObjArray_shrink(&arr));
    TEST_ASSERT_EQUAL_size_t(2, arr.capacity);

    TestObjArray_destroy(&arr);
}

/* Reserve tests */

void test_reserve_increases_capacity(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, NULL);

    TEST_ASSERT_EQUAL_INT(0, TestObjArray_reserve(&arr, 100));
    TEST_ASSERT_EQUAL_size_t(100, arr.capacity);

    TestObjArray_destroy(&arr);
}

/* Steal tests */

void test_steal_transfers_ownership(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "first");
    TestObj* obj2 = test_obj_create(2, "second");
    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);

    size_t count;
    TestObj** stolen = TestObjArray_steal(&arr, &count);

    TEST_ASSERT_NOT_NULL(stolen);
    TEST_ASSERT_EQUAL_size_t(2, count);
    TEST_ASSERT_EQUAL_PTR(obj1, stolen[0]);
    TEST_ASSERT_EQUAL_PTR(obj2, stolen[1]);

    /* Array is now empty */
    TEST_ASSERT_NULL(arr.data);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    TEST_ASSERT_EQUAL_size_t(0, arr.capacity);

    /* Manual cleanup of stolen data */
    test_obj_free(stolen[0]);
    test_obj_free(stolen[1]);
    free(stolen);
}

/* Find tests */

void test_find_returns_index(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "first");
    TestObj* obj2 = test_obj_create(2, "second");
    TestObj* obj3 = test_obj_create(3, "third");
    TestObjArray_push(&arr, obj1);
    TestObjArray_push(&arr, obj2);
    TestObjArray_push(&arr, obj3);

    TEST_ASSERT_EQUAL_INT(0, TestObjArray_find(&arr, obj1));
    TEST_ASSERT_EQUAL_INT(1, TestObjArray_find(&arr, obj2));
    TEST_ASSERT_EQUAL_INT(2, TestObjArray_find(&arr, obj3));

    TestObjArray_destroy(&arr);
}

void test_find_not_found_returns_negative(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    TestObj* obj1 = test_obj_create(1, "first");
    TestObj* other = test_obj_create(99, "not in array");
    TestObjArray_push(&arr, obj1);

    TEST_ASSERT_EQUAL_INT(-1, TestObjArray_find(&arr, other));

    test_obj_free(other);
    TestObjArray_destroy(&arr);
}

/* StringArray tests (pre-defined type) */

void test_string_array_basic_ops(void) {
    StringArray arr = {0};
    StringArray_init(&arr, free);  /* Takes ownership */

    StringArray_push(&arr, strdup("hello"));
    StringArray_push(&arr, strdup("world"));

    TEST_ASSERT_EQUAL_size_t(2, arr.count);
    TEST_ASSERT_EQUAL_STRING("hello", StringArray_get(&arr, 0));
    TEST_ASSERT_EQUAL_STRING("world", StringArray_get(&arr, 1));

    StringArray_destroy(&arr);
}

void test_string_array_pop(void) {
    StringArray arr = {0};
    StringArray_init(&arr, NULL);  /* Non-owning */

    char* s1 = strdup("first");
    char* s2 = strdup("second");
    StringArray_push(&arr, s1);
    StringArray_push(&arr, s2);

    char* popped = StringArray_pop(&arr);
    TEST_ASSERT_EQUAL_STRING("second", popped);
    TEST_ASSERT_EQUAL_size_t(1, arr.count);

    free(s1);
    free(popped);
    StringArray_destroy(&arr);
}

void test_string_array_clear_frees_strings(void) {
    StringArray arr = {0};
    StringArray_init(&arr, free);

    StringArray_push(&arr, strdup("a"));
    StringArray_push(&arr, strdup("b"));
    StringArray_push(&arr, strdup("c"));

    StringArray_clear(&arr);
    TEST_ASSERT_EQUAL_size_t(0, arr.count);
    /* Strings were freed by clear */

    StringArray_destroy(&arr);
}

/* Stress test */

void test_large_pointer_array(void) {
    TestObjArray arr = {0};
    TestObjArray_init(&arr, test_obj_free);

    const int N = 1000;
    for (int i = 0; i < N; i++) {
        char name[32] = {0};  /* Initialize to avoid valgrind warnings */
        snprintf(name, sizeof(name), "item_%d", i);
        TEST_ASSERT_EQUAL_INT(0, TestObjArray_push(&arr, test_obj_create(i, name)));
    }

    TEST_ASSERT_EQUAL_size_t(N, arr.count);

    /* Verify elements */
    for (int i = 0; i < N; i++) {
        TestObj* obj = TestObjArray_get(&arr, i);
        TEST_ASSERT_NOT_NULL(obj);
        TEST_ASSERT_EQUAL_INT(i, obj->id);
    }

    TestObjArray_destroy(&arr);
    TEST_ASSERT_EQUAL_INT(N, destructor_call_count);
}

int main(void) {
    UNITY_BEGIN();

    /* Initialization tests */
    RUN_TEST(test_init_creates_empty_array);
    RUN_TEST(test_init_with_destructor);
    RUN_TEST(test_init_with_custom_capacity);
    RUN_TEST(test_init_null_returns_error);

    /* Destroy tests */
    RUN_TEST(test_destroy_calls_destructor);
    RUN_TEST(test_destroy_shallow_does_not_call_destructor);
    RUN_TEST(test_destroy_null_is_safe);

    /* Push tests */
    RUN_TEST(test_push_adds_element);
    RUN_TEST(test_push_grows_capacity);
    RUN_TEST(test_push_null_array_returns_error);

    /* Pop tests */
    RUN_TEST(test_pop_returns_last_element);
    RUN_TEST(test_pop_empty_returns_null);

    /* Get tests */
    RUN_TEST(test_get_returns_element);
    RUN_TEST(test_get_out_of_bounds_returns_null);
    RUN_TEST(test_get_const_correctness);

    /* Set tests */
    RUN_TEST(test_set_replaces_element_and_calls_destructor);
    RUN_TEST(test_set_without_destructor_does_not_free);

    /* Insert tests */
    RUN_TEST(test_insert_at_beginning);
    RUN_TEST(test_insert_in_middle);

    /* Remove tests */
    RUN_TEST(test_remove_returns_element_without_freeing);
    RUN_TEST(test_remove_out_of_bounds_returns_null);

    /* Delete tests */
    RUN_TEST(test_delete_removes_and_frees);

    /* Clear tests */
    RUN_TEST(test_clear_frees_all_elements);
    RUN_TEST(test_clear_shallow_does_not_free);

    /* Shrink tests */
    RUN_TEST(test_shrink_reduces_capacity);

    /* Reserve tests */
    RUN_TEST(test_reserve_increases_capacity);

    /* Steal tests */
    RUN_TEST(test_steal_transfers_ownership);

    /* Find tests */
    RUN_TEST(test_find_returns_index);
    RUN_TEST(test_find_not_found_returns_negative);

    /* StringArray tests */
    RUN_TEST(test_string_array_basic_ops);
    RUN_TEST(test_string_array_pop);
    RUN_TEST(test_string_array_clear_frees_strings);

    /* Stress test */
    RUN_TEST(test_large_pointer_array);

    return UNITY_END();
}
