#include "unity/unity.h"
#include "todo_tool.h"
#include <string.h>
#include <stdlib.h>

static TodoTool test_tool;

void setUp(void) {
    todo_tool_init(&test_tool);
}

void tearDown(void) {
    todo_tool_destroy(&test_tool);
}

void test_todo_tool_init(void) {
    TodoTool tool = {0};
    TEST_ASSERT_EQUAL_INT(0, todo_tool_init(&tool));
    TEST_ASSERT_NOT_NULL(tool.todo_list);
    todo_tool_destroy(&tool);
}

void test_todo_tool_init_null_parameter(void) {
    TEST_ASSERT_EQUAL_INT(-1, todo_tool_init(NULL));
}

void test_todo_tool_create(void) {
    char* result = todo_tool_create(&test_tool, "Test task", "high");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "Test task") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"priority\":\"high\"") != NULL);
    free(result);
}

void test_todo_tool_create_null_parameters(void) {
    char* result = todo_tool_create(NULL, "test", "low");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
    
    result = todo_tool_create(&test_tool, NULL, "low");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
}

void test_todo_tool_update_status(void) {
    char* create_result = todo_tool_create(&test_tool, "Update test", "medium");
    
    char id[64] = {0};
    char* id_start = strstr(create_result, "\"id\":\"") + 6;
    char* id_end = strchr(id_start, '"');
    strncpy(id, id_start, id_end - id_start);
    free(create_result);
    
    char* result = todo_tool_update_status(&test_tool, id, "in_progress");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"status\":\"in_progress\"") != NULL);
    free(result);
}

void test_todo_tool_update_status_null_parameters(void) {
    char* result = todo_tool_update_status(NULL, "id", "completed");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
    
    result = todo_tool_update_status(&test_tool, NULL, "completed");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
    
    result = todo_tool_update_status(&test_tool, "id", NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
}

void test_todo_tool_update_priority(void) {
    char* create_result = todo_tool_create(&test_tool, "Priority test", "low");
    
    char id[64] = {0};
    char* id_start = strstr(create_result, "\"id\":\"") + 6;
    char* id_end = strchr(id_start, '"');
    strncpy(id, id_start, id_end - id_start);
    free(create_result);
    
    char* result = todo_tool_update_priority(&test_tool, id, "high");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "\"priority\":\"high\"") != NULL);
    free(result);
}

void test_todo_tool_delete(void) {
    char* create_result = todo_tool_create(&test_tool, "Delete test", "medium");
    
    char id[64] = {0};
    char* id_start = strstr(create_result, "\"id\":\"") + 6;
    char* id_end = strchr(id_start, '"');
    strncpy(id, id_start, id_end - id_start);
    free(create_result);
    
    char* result = todo_tool_delete(&test_tool, id);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"success\":true") != NULL);
    TEST_ASSERT_TRUE(strstr(result, id) != NULL);
    free(result);
}

void test_todo_tool_delete_null_parameters(void) {
    char* result = todo_tool_delete(NULL, "id");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
    
    result = todo_tool_delete(&test_tool, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
}

void test_todo_tool_list_empty(void) {
    char* result = todo_tool_list(&test_tool, NULL, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"todos\":[]") != NULL);
    free(result);
}

void test_todo_tool_list_with_todos(void) {
    todo_tool_create(&test_tool, "Task 1", "high");
    todo_tool_create(&test_tool, "Task 2", "low");
    
    char* result = todo_tool_list(&test_tool, NULL, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Task 1") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "Task 2") != NULL);
    free(result);
}

void test_todo_tool_list_null_parameter(void) {
    char* result = todo_tool_list(NULL, NULL, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
}

void test_todo_tool_serialize(void) {
    todo_tool_create(&test_tool, "Serialize test", "medium");
    
    char* result = todo_tool_serialize(&test_tool);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"todos\":[") != NULL);
    TEST_ASSERT_TRUE(strstr(result, "Serialize test") != NULL);
    free(result);
}

void test_todo_tool_serialize_null_parameter(void) {
    char* result = todo_tool_serialize(NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
}

void test_todo_tool_execute_list(void) {
    todo_tool_create(&test_tool, "Execute test", "high");
    
    char* result = todo_tool_execute(&test_tool, "list", NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Execute test") != NULL);
    free(result);
}

void test_todo_tool_execute_serialize(void) {
    todo_tool_create(&test_tool, "Execute serialize", "low");
    
    char* result = todo_tool_execute(&test_tool, "serialize", NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "Execute serialize") != NULL);
    free(result);
}

void test_todo_tool_execute_unknown_action(void) {
    char* result = todo_tool_execute(&test_tool, "unknown", NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\":\"Unknown action\"") != NULL);
    free(result);
}

void test_todo_tool_execute_null_parameters(void) {
    char* result = todo_tool_execute(NULL, "list", NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
    
    result = todo_tool_execute(&test_tool, NULL, NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_TRUE(strstr(result, "\"error\"") != NULL);
    free(result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_todo_tool_init);
    RUN_TEST(test_todo_tool_init_null_parameter);
    RUN_TEST(test_todo_tool_create);
    RUN_TEST(test_todo_tool_create_null_parameters);
    RUN_TEST(test_todo_tool_update_status);
    RUN_TEST(test_todo_tool_update_status_null_parameters);
    RUN_TEST(test_todo_tool_update_priority);
    RUN_TEST(test_todo_tool_delete);
    RUN_TEST(test_todo_tool_delete_null_parameters);
    RUN_TEST(test_todo_tool_list_empty);
    RUN_TEST(test_todo_tool_list_with_todos);
    RUN_TEST(test_todo_tool_list_null_parameter);
    RUN_TEST(test_todo_tool_serialize);
    RUN_TEST(test_todo_tool_serialize_null_parameter);
    RUN_TEST(test_todo_tool_execute_list);
    RUN_TEST(test_todo_tool_execute_serialize);
    RUN_TEST(test_todo_tool_execute_unknown_action);
    RUN_TEST(test_todo_tool_execute_null_parameters);
    
    return UNITY_END();
}