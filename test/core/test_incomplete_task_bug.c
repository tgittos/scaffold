#include "unity/unity.h"
#include "ralph.h"
#include "lib/tools/todo_manager.h"
#include "db/task_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util/ralph_home.h"

void setUp(void) {
    ralph_home_init(NULL);
}

void tearDown(void) {

    ralph_home_cleanup();
}

void test_todo_has_pending_tasks(void) {
    printf("\n=== Testing todo_has_pending_tasks function ===\n");
    
    TodoList list;
    TEST_ASSERT_EQUAL(0, todo_list_init(&list));
    
    // Initially, no pending tasks
    TEST_ASSERT_EQUAL(0, todo_has_pending_tasks(&list));
    
    // Add a pending task
    char id1[TODO_MAX_ID_LENGTH];
    TEST_ASSERT_EQUAL(0, todo_create(&list, "Test task 1", TODO_PRIORITY_HIGH, id1));
    TEST_ASSERT_EQUAL(1, todo_has_pending_tasks(&list));
    
    // Add another pending task
    char id2[TODO_MAX_ID_LENGTH];
    TEST_ASSERT_EQUAL(0, todo_create(&list, "Test task 2", TODO_PRIORITY_MEDIUM, id2));
    TEST_ASSERT_EQUAL(1, todo_has_pending_tasks(&list));
    
    // Update first task to in_progress
    TEST_ASSERT_EQUAL(0, todo_update_status(&list, id1, TODO_STATUS_IN_PROGRESS));
    TEST_ASSERT_EQUAL(1, todo_has_pending_tasks(&list)); // Still has pending (in_progress counts)
    
    // Update both to completed
    TEST_ASSERT_EQUAL(0, todo_update_status(&list, id1, TODO_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL(0, todo_update_status(&list, id2, TODO_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL(0, todo_has_pending_tasks(&list)); // No more pending
    
    todo_list_destroy(&list);
    printf("todo_has_pending_tasks function works correctly!\n");
}

void test_incomplete_task_bug_integration(void) {
    printf("\n=== Testing Incomplete Task Bug Fix (Integration) ===\n");
    
    // This test verifies the fix by creating a scenario where:
    // 1. A todo list is created with pending tasks
    // 2. The session has pending todos
    // 3. The fix ensures processing continues when there are pending todos
    
    AgentSession session;
    TEST_ASSERT_EQUAL(0, session_init(&session));
    
    // Create some pending todos in the session
    char id1[TODO_MAX_ID_LENGTH];
    char id2[TODO_MAX_ID_LENGTH];
    char id3[TODO_MAX_ID_LENGTH];
    
    TEST_ASSERT_EQUAL(0, todo_create(&session.todo_list, "Analyze directory structure", TODO_PRIORITY_HIGH, id1));
    TEST_ASSERT_EQUAL(0, todo_create(&session.todo_list, "Identify main components", TODO_PRIORITY_HIGH, id2));
    TEST_ASSERT_EQUAL(0, todo_create(&session.todo_list, "Document findings", TODO_PRIORITY_HIGH, id3));
    
    // Verify the session has pending todos
    TEST_ASSERT_EQUAL(1, todo_has_pending_tasks(&session.todo_list));
    
    // Update one to in_progress
    TEST_ASSERT_EQUAL(0, todo_update_status(&session.todo_list, id1, TODO_STATUS_IN_PROGRESS));
    TEST_ASSERT_EQUAL(1, todo_has_pending_tasks(&session.todo_list)); // Still has pending
    
    // Complete all tasks
    TEST_ASSERT_EQUAL(0, todo_update_status(&session.todo_list, id1, TODO_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL(0, todo_update_status(&session.todo_list, id2, TODO_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL(0, todo_update_status(&session.todo_list, id3, TODO_STATUS_COMPLETED));
    
    // Verify no pending tasks remain
    TEST_ASSERT_EQUAL(0, todo_has_pending_tasks(&session.todo_list));
    
    session_cleanup(&session);
    printf("Fix integration test passed - todo_has_pending_tasks correctly identifies incomplete work!\n");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_todo_has_pending_tasks);
    RUN_TEST(test_incomplete_task_bug_integration);
    return UNITY_END();
}