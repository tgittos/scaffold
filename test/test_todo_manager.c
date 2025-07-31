#include "unity/unity.h"
#include "../src/todo_manager.h"
#include <string.h>
#include <stdlib.h>

static TodoList test_list;

void setUp(void) {
    todo_list_init(&test_list);
}

void tearDown(void) {
    todo_list_destroy(&test_list);
}

void test_todo_list_init(void) {
    TodoList list = {0};
    TEST_ASSERT_EQUAL_INT(0, todo_list_init(&list));
    TEST_ASSERT_NOT_NULL(list.todos);
    TEST_ASSERT_EQUAL_UINT(0, list.count);
    TEST_ASSERT_GREATER_THAN_UINT(0, list.capacity);
    todo_list_destroy(&list);
}

void test_todo_list_init_null_parameter(void) {
    TEST_ASSERT_EQUAL_INT(-1, todo_list_init(NULL));
}

void test_todo_create_basic(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    int result = todo_create(&test_list, "Test task", TODO_PRIORITY_MEDIUM, id);
    
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_UINT(1, test_list.count);
    TEST_ASSERT_NOT_EQUAL(0, strlen(id));
    
    Todo* todo = todo_find_by_id(&test_list, id);
    TEST_ASSERT_NOT_NULL(todo);
    TEST_ASSERT_EQUAL_STRING("Test task", todo->content);
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_PENDING, todo->status);
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_MEDIUM, todo->priority);
}

void test_todo_create_null_parameters(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    
    TEST_ASSERT_EQUAL_INT(-1, todo_create(NULL, "test", TODO_PRIORITY_LOW, id));
    TEST_ASSERT_EQUAL_INT(-1, todo_create(&test_list, NULL, TODO_PRIORITY_LOW, id));
    TEST_ASSERT_EQUAL_INT(-1, todo_create(&test_list, "test", TODO_PRIORITY_LOW, NULL));
}

void test_todo_create_content_too_long(void) {
    static const char long_content[] = 
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    
    char id[TODO_MAX_ID_LENGTH] = {0};
    TEST_ASSERT_EQUAL_INT(-1, todo_create(&test_list, long_content, TODO_PRIORITY_LOW, id));
}

void test_todo_find_by_id(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "Find me", TODO_PRIORITY_HIGH, id);
    
    Todo* found = todo_find_by_id(&test_list, id);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Find me", found->content);
    
    Todo* not_found = todo_find_by_id(&test_list, "nonexistent");
    TEST_ASSERT_NULL(not_found);
}

void test_todo_find_by_id_null_parameters(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "test", TODO_PRIORITY_LOW, id);
    
    TEST_ASSERT_NULL(todo_find_by_id(NULL, id));
    TEST_ASSERT_NULL(todo_find_by_id(&test_list, NULL));
}

void test_todo_update_status(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "Update me", TODO_PRIORITY_LOW, id);
    
    TEST_ASSERT_EQUAL_INT(0, todo_update_status(&test_list, id, TODO_STATUS_IN_PROGRESS));
    
    Todo* todo = todo_find_by_id(&test_list, id);
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_IN_PROGRESS, todo->status);
    
    TEST_ASSERT_EQUAL_INT(0, todo_update_status(&test_list, id, TODO_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_COMPLETED, todo->status);
}

void test_todo_update_status_null_parameters(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "test", TODO_PRIORITY_LOW, id);
    
    TEST_ASSERT_EQUAL_INT(-1, todo_update_status(NULL, id, TODO_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL_INT(-1, todo_update_status(&test_list, NULL, TODO_STATUS_COMPLETED));
}

void test_todo_update_status_nonexistent_id(void) {
    TEST_ASSERT_EQUAL_INT(-1, todo_update_status(&test_list, "nonexistent", TODO_STATUS_COMPLETED));
}

void test_todo_update_priority(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "Priority test", TODO_PRIORITY_LOW, id);
    
    TEST_ASSERT_EQUAL_INT(0, todo_update_priority(&test_list, id, TODO_PRIORITY_HIGH));
    
    Todo* todo = todo_find_by_id(&test_list, id);
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_HIGH, todo->priority);
}

void test_todo_update_priority_null_parameters(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "test", TODO_PRIORITY_LOW, id);
    
    TEST_ASSERT_EQUAL_INT(-1, todo_update_priority(NULL, id, TODO_PRIORITY_HIGH));
    TEST_ASSERT_EQUAL_INT(-1, todo_update_priority(&test_list, NULL, TODO_PRIORITY_HIGH));
}

void test_todo_delete(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "Delete me", TODO_PRIORITY_LOW, id);
    TEST_ASSERT_EQUAL_UINT(1, test_list.count);
    
    TEST_ASSERT_EQUAL_INT(0, todo_delete(&test_list, id));
    TEST_ASSERT_EQUAL_UINT(0, test_list.count);
    TEST_ASSERT_NULL(todo_find_by_id(&test_list, id));
}

void test_todo_delete_null_parameters(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "test", TODO_PRIORITY_LOW, id);
    
    TEST_ASSERT_EQUAL_INT(-1, todo_delete(NULL, id));
    TEST_ASSERT_EQUAL_INT(-1, todo_delete(&test_list, NULL));
}

void test_todo_delete_nonexistent_id(void) {
    TEST_ASSERT_EQUAL_INT(-1, todo_delete(&test_list, "nonexistent"));
}

void test_todo_list_filter(void) {
    char id1[TODO_MAX_ID_LENGTH], id2[TODO_MAX_ID_LENGTH], id3[TODO_MAX_ID_LENGTH];
    
    todo_create(&test_list, "Task 1", TODO_PRIORITY_LOW, id1);
    todo_create(&test_list, "Task 2", TODO_PRIORITY_HIGH, id2);
    todo_create(&test_list, "Task 3", TODO_PRIORITY_MEDIUM, id3);
    
    todo_update_status(&test_list, id1, TODO_STATUS_COMPLETED);
    todo_update_status(&test_list, id2, TODO_STATUS_IN_PROGRESS);
    
    Todo* filtered = NULL;
    size_t count = 0;
    
    TEST_ASSERT_EQUAL_INT(0, todo_list_filter(&test_list, TODO_STATUS_PENDING, TODO_PRIORITY_LOW, &filtered, &count));
    TEST_ASSERT_EQUAL_UINT(1, count);
    TEST_ASSERT_EQUAL_STRING("Task 3", filtered[0].content);
    free(filtered);
    
    TEST_ASSERT_EQUAL_INT(0, todo_list_filter(&test_list, -1, TODO_PRIORITY_MEDIUM, &filtered, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    free(filtered);
}

void test_todo_serialize_json(void) {
    char id[TODO_MAX_ID_LENGTH] = {0};
    todo_create(&test_list, "Serialize test", TODO_PRIORITY_HIGH, id);
    
    char* json = todo_serialize_json(&test_list);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_TRUE(strstr(json, "\"todos\":[") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "Serialize test") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"status\":\"pending\"") != NULL);
    TEST_ASSERT_TRUE(strstr(json, "\"priority\":\"high\"") != NULL);
    
    free(json);
}

void test_todo_serialize_json_empty_list(void) {
    char* json = todo_serialize_json(&test_list);
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL_STRING("{\"todos\":[]}", json);
    free(json);
}

void test_todo_serialize_json_null_parameter(void) {
    TEST_ASSERT_NULL(todo_serialize_json(NULL));
}

void test_todo_status_string_conversion(void) {
    TEST_ASSERT_EQUAL_STRING("pending", todo_status_to_string(TODO_STATUS_PENDING));
    TEST_ASSERT_EQUAL_STRING("in_progress", todo_status_to_string(TODO_STATUS_IN_PROGRESS));
    TEST_ASSERT_EQUAL_STRING("completed", todo_status_to_string(TODO_STATUS_COMPLETED));
    
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_PENDING, todo_status_from_string("pending"));
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_IN_PROGRESS, todo_status_from_string("in_progress"));
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_COMPLETED, todo_status_from_string("completed"));
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_PENDING, todo_status_from_string("invalid"));
    TEST_ASSERT_EQUAL_INT(TODO_STATUS_PENDING, todo_status_from_string(NULL));
}

void test_todo_priority_string_conversion(void) {
    TEST_ASSERT_EQUAL_STRING("low", todo_priority_to_string(TODO_PRIORITY_LOW));
    TEST_ASSERT_EQUAL_STRING("medium", todo_priority_to_string(TODO_PRIORITY_MEDIUM));
    TEST_ASSERT_EQUAL_STRING("high", todo_priority_to_string(TODO_PRIORITY_HIGH));
    
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_LOW, todo_priority_from_string("low"));
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_MEDIUM, todo_priority_from_string("medium"));
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_HIGH, todo_priority_from_string("high"));
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_LOW, todo_priority_from_string("invalid"));
    TEST_ASSERT_EQUAL_INT(TODO_PRIORITY_LOW, todo_priority_from_string(NULL));
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_todo_list_init);
    RUN_TEST(test_todo_list_init_null_parameter);
    RUN_TEST(test_todo_create_basic);
    RUN_TEST(test_todo_create_null_parameters);
    RUN_TEST(test_todo_create_content_too_long);
    RUN_TEST(test_todo_find_by_id);
    RUN_TEST(test_todo_find_by_id_null_parameters);
    RUN_TEST(test_todo_update_status);
    RUN_TEST(test_todo_update_status_null_parameters);
    RUN_TEST(test_todo_update_status_nonexistent_id);
    RUN_TEST(test_todo_update_priority);
    RUN_TEST(test_todo_update_priority_null_parameters);
    RUN_TEST(test_todo_delete);
    RUN_TEST(test_todo_delete_null_parameters);
    RUN_TEST(test_todo_delete_nonexistent_id);
    RUN_TEST(test_todo_list_filter);
    RUN_TEST(test_todo_serialize_json);
    RUN_TEST(test_todo_serialize_json_empty_list);
    RUN_TEST(test_todo_serialize_json_null_parameter);
    RUN_TEST(test_todo_status_string_conversion);
    RUN_TEST(test_todo_priority_string_conversion);
    
    return UNITY_END();
}