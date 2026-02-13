#include "unity.h"
#include "db/task_store.h"
#include "util/uuid_utils.h"
#include "util/app_home.h"
#include "../test_fs_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static char g_test_db_path[256];
static task_store_t* g_store = NULL;
static char g_session_id[40];

void setUp(void) {
    app_home_init(NULL);

    snprintf(g_test_db_path, sizeof(g_test_db_path), "/tmp/test_tasks_%d.db", getpid());
    unlink_sqlite_db(g_test_db_path);
    g_store = task_store_create(g_test_db_path);

    uuid_generate_v4(g_session_id);
}

void tearDown(void) {
    if (g_store != NULL) {
        task_store_destroy(g_store);
        g_store = NULL;
    }

    unlink_sqlite_db(g_test_db_path);

    app_home_cleanup();
}

// =============================================================================
// UUID Utils Tests
// =============================================================================

void test_uuid_generate_v4(void) {
    char uuid1[40] = {0};
    char uuid2[40] = {0};

    int result = uuid_generate_v4(uuid1);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(36, strlen(uuid1));
    TEST_ASSERT_TRUE(uuid_is_valid(uuid1));

    result = uuid_generate_v4(uuid2);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(36, strlen(uuid2));
    TEST_ASSERT_TRUE(uuid_is_valid(uuid2));

    // Two UUIDs should be different
    TEST_ASSERT_NOT_EQUAL(0, strcmp(uuid1, uuid2));
}

void test_uuid_is_valid(void) {
    TEST_ASSERT_TRUE(uuid_is_valid("12345678-1234-1234-1234-123456789abc"));
    TEST_ASSERT_TRUE(uuid_is_valid("ABCDEF12-1234-1234-1234-123456789ABC"));
    TEST_ASSERT_TRUE(uuid_is_valid("abcdef12-1234-1234-1234-123456789abc"));

    TEST_ASSERT_FALSE(uuid_is_valid(NULL));
    TEST_ASSERT_FALSE(uuid_is_valid(""));
    TEST_ASSERT_FALSE(uuid_is_valid("not-a-uuid"));
    TEST_ASSERT_FALSE(uuid_is_valid("12345678-1234-1234-1234-123456789ab"));  // Too short
    TEST_ASSERT_FALSE(uuid_is_valid("12345678-1234-1234-1234-123456789abcd")); // Too long
    TEST_ASSERT_FALSE(uuid_is_valid("1234567801234-1234-1234-123456789abc")); // Missing hyphen
    TEST_ASSERT_FALSE(uuid_is_valid("12345678-1234-1234-1234-12345678ZZZZ")); // Invalid hex
}

// =============================================================================
// Task Store Basic Tests
// =============================================================================

void test_task_store_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(g_store);
    // tearDown will destroy it
}

void test_task_store_multiple_instances(void) {
    char other_path[256];
    snprintf(other_path, sizeof(other_path), "/tmp/test_tasks2_%d.db", getpid());
    task_store_t* store2 = task_store_create(other_path);
    TEST_ASSERT_NOT_NULL(store2);
    TEST_ASSERT_NOT_EQUAL(g_store, store2);

    task_store_destroy(store2);
    unlink_sqlite_db(other_path);
}

// =============================================================================
// CRUD Operations Tests
// =============================================================================

void test_task_store_create_task(void) {
    char task_id[40];

    int result = task_store_create_task(g_store, g_session_id, "Test task content",
                                        TASK_PRIORITY_MEDIUM, NULL, task_id);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(uuid_is_valid(task_id));
}

void test_task_store_create_task_null_params(void) {
    char task_id[40];

    // NULL store
    int result = task_store_create_task(NULL, g_session_id, "content",
                                        TASK_PRIORITY_MEDIUM, NULL, task_id);
    TEST_ASSERT_EQUAL(-1, result);

    // NULL session_id
    result = task_store_create_task(g_store, NULL, "content",
                                    TASK_PRIORITY_MEDIUM, NULL, task_id);
    TEST_ASSERT_EQUAL(-1, result);

    // NULL content
    result = task_store_create_task(g_store, g_session_id, NULL,
                                    TASK_PRIORITY_MEDIUM, NULL, task_id);
    TEST_ASSERT_EQUAL(-1, result);

    // NULL out_id
    result = task_store_create_task(g_store, g_session_id, "content",
                                    TASK_PRIORITY_MEDIUM, NULL, NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_task_store_get_task(void) {
    char task_id[40];

    int result = task_store_create_task(g_store, g_session_id, "Get task test",
                                        TASK_PRIORITY_HIGH, NULL, task_id);
    TEST_ASSERT_EQUAL(0, result);

    Task* task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_NOT_NULL(task);
    TEST_ASSERT_EQUAL_STRING(task_id, task->id);
    TEST_ASSERT_EQUAL_STRING(g_session_id, task->session_id);
    TEST_ASSERT_EQUAL_STRING("Get task test", task->content);
    TEST_ASSERT_EQUAL(TASK_STATUS_PENDING, task->status);
    TEST_ASSERT_EQUAL(TASK_PRIORITY_HIGH, task->priority);
    TEST_ASSERT_EQUAL_STRING("", task->parent_id);

    task_free(task);
}

void test_task_store_get_nonexistent_task(void) {
    Task* task = task_store_get_task(g_store, "nonexistent-uuid-1234-1234-123456789abc");
    TEST_ASSERT_NULL(task);
}

void test_task_store_update_status(void) {
    char task_id[40];

    task_store_create_task(g_store, g_session_id, "Status test",
                          TASK_PRIORITY_MEDIUM, NULL, task_id);

    // Update to in_progress
    int result = task_store_update_status(g_store, task_id, TASK_STATUS_IN_PROGRESS);
    TEST_ASSERT_EQUAL(0, result);

    Task* task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_EQUAL(TASK_STATUS_IN_PROGRESS, task->status);
    task_free(task);

    // Update to completed
    result = task_store_update_status(g_store, task_id, TASK_STATUS_COMPLETED);
    TEST_ASSERT_EQUAL(0, result);

    task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_EQUAL(TASK_STATUS_COMPLETED, task->status);
    task_free(task);
}

void test_task_store_update_content(void) {
    char task_id[40];

    task_store_create_task(g_store, g_session_id, "Original content",
                          TASK_PRIORITY_MEDIUM, NULL, task_id);

    int result = task_store_update_content(g_store, task_id, "Updated content");
    TEST_ASSERT_EQUAL(0, result);

    Task* task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_EQUAL_STRING("Updated content", task->content);
    task_free(task);
}

void test_task_store_update_priority(void) {
    char task_id[40];

    task_store_create_task(g_store, g_session_id, "Priority test",
                          TASK_PRIORITY_LOW, NULL, task_id);

    int result = task_store_update_priority(g_store, task_id, TASK_PRIORITY_HIGH);
    TEST_ASSERT_EQUAL(0, result);

    Task* task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_EQUAL(TASK_PRIORITY_HIGH, task->priority);
    task_free(task);
}

void test_task_store_delete_task(void) {
    char task_id[40];

    task_store_create_task(g_store, g_session_id, "Delete test",
                          TASK_PRIORITY_MEDIUM, NULL, task_id);

    // Verify task exists
    Task* task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_NOT_NULL(task);
    task_free(task);

    // Delete task
    int result = task_store_delete_task(g_store, task_id);
    TEST_ASSERT_EQUAL(0, result);

    // Verify task no longer exists
    task = task_store_get_task(g_store, task_id);
    TEST_ASSERT_NULL(task);
}

void test_task_store_delete_nonexistent_task(void) {
    int result = task_store_delete_task(g_store, "nonexistent-uuid-1234-1234-123456789abc");
    TEST_ASSERT_EQUAL(-1, result);
}

// =============================================================================
// Parent/Child Relationship Tests
// =============================================================================

void test_task_store_create_subtask(void) {
    char parent_id[40];
    char child_id[40];

    // Create parent task
    task_store_create_task(g_store, g_session_id, "Parent task",
                          TASK_PRIORITY_HIGH, NULL, parent_id);

    // Create child task
    int result = task_store_create_task(g_store, g_session_id, "Child task",
                                        TASK_PRIORITY_MEDIUM, parent_id, child_id);
    TEST_ASSERT_EQUAL(0, result);

    // Verify child has correct parent
    Task* child = task_store_get_task(g_store, child_id);
    TEST_ASSERT_EQUAL_STRING(parent_id, child->parent_id);
    task_free(child);
}

void test_task_store_get_children(void) {
    char parent_id[40];
    char child1_id[40];
    char child2_id[40];

    // Create parent
    task_store_create_task(g_store, g_session_id, "Parent",
                          TASK_PRIORITY_HIGH, NULL, parent_id);

    // Create children
    task_store_create_task(g_store, g_session_id, "Child 1",
                          TASK_PRIORITY_MEDIUM, parent_id, child1_id);
    task_store_create_task(g_store, g_session_id, "Child 2",
                          TASK_PRIORITY_LOW, parent_id, child2_id);

    // Get children
    size_t count = 0;
    Task** children = task_store_get_children(g_store, parent_id, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(children);

    // Verify children are the correct tasks
    int found_child1 = 0, found_child2 = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(children[i]->id, child1_id) == 0) found_child1 = 1;
        if (strcmp(children[i]->id, child2_id) == 0) found_child2 = 1;
    }
    TEST_ASSERT_TRUE(found_child1);
    TEST_ASSERT_TRUE(found_child2);

    task_free_list(children, count);
}

void test_task_store_get_subtree(void) {
    char root_id[40];
    char child1_id[40];
    char child2_id[40];
    char grandchild_id[40];

    // Create hierarchy: root -> child1 -> grandchild, root -> child2
    task_store_create_task(g_store, g_session_id, "Root",
                          TASK_PRIORITY_HIGH, NULL, root_id);
    task_store_create_task(g_store, g_session_id, "Child 1",
                          TASK_PRIORITY_MEDIUM, root_id, child1_id);
    task_store_create_task(g_store, g_session_id, "Child 2",
                          TASK_PRIORITY_MEDIUM, root_id, child2_id);
    task_store_create_task(g_store, g_session_id, "Grandchild",
                          TASK_PRIORITY_LOW, child1_id, grandchild_id);

    // Get subtree should include child1, child2, and grandchild (not root)
    size_t count = 0;
    Task** subtree = task_store_get_subtree(g_store, root_id, &count);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_NOT_NULL(subtree);

    task_free_list(subtree, count);
}

void test_task_store_delete_parent_cascades(void) {
    char parent_id[40];
    char child_id[40];

    // Create parent and child
    task_store_create_task(g_store, g_session_id, "Parent",
                          TASK_PRIORITY_HIGH, NULL, parent_id);
    task_store_create_task(g_store, g_session_id, "Child",
                          TASK_PRIORITY_MEDIUM, parent_id, child_id);

    // Verify child exists
    Task* child = task_store_get_task(g_store, child_id);
    TEST_ASSERT_NOT_NULL(child);
    task_free(child);

    // Delete parent
    task_store_delete_task(g_store, parent_id);

    // Verify child was cascade deleted
    child = task_store_get_task(g_store, child_id);
    TEST_ASSERT_NULL(child);
}

void test_task_store_set_parent(void) {
    char task1_id[40];
    char task2_id[40];

    // Create two independent tasks
    task_store_create_task(g_store, g_session_id, "Task 1",
                          TASK_PRIORITY_MEDIUM, NULL, task1_id);
    task_store_create_task(g_store, g_session_id, "Task 2",
                          TASK_PRIORITY_MEDIUM, NULL, task2_id);

    // Reparent task2 under task1
    int result = task_store_set_parent(g_store, task2_id, task1_id);
    TEST_ASSERT_EQUAL(0, result);

    Task* task2 = task_store_get_task(g_store, task2_id);
    TEST_ASSERT_EQUAL_STRING(task1_id, task2->parent_id);
    task_free(task2);

    // Remove parent (make task2 root again)
    result = task_store_set_parent(g_store, task2_id, NULL);
    TEST_ASSERT_EQUAL(0, result);

    task2 = task_store_get_task(g_store, task2_id);
    TEST_ASSERT_EQUAL_STRING("", task2->parent_id);
    task_free(task2);
}

// =============================================================================
// Dependency Relationship Tests
// =============================================================================

void test_task_store_add_dependency(void) {
    char task1_id[40];
    char task2_id[40];

    // Create two tasks
    task_store_create_task(g_store, g_session_id, "Blocker task",
                          TASK_PRIORITY_HIGH, NULL, task1_id);
    task_store_create_task(g_store, g_session_id, "Blocked task",
                          TASK_PRIORITY_MEDIUM, NULL, task2_id);

    // Add dependency: task2 is blocked by task1
    int result = task_store_add_dependency(g_store, task2_id, task1_id);
    TEST_ASSERT_EQUAL(0, result);

    // Verify dependency was created
    size_t count = 0;
    char** blockers = task_store_get_blockers(g_store, task2_id, &count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(blockers);
    TEST_ASSERT_EQUAL_STRING(task1_id, blockers[0]);

    task_free_id_list(blockers, count);
}

void test_task_store_remove_dependency(void) {
    char task1_id[40];
    char task2_id[40];

    // Create two tasks and add dependency
    task_store_create_task(g_store, g_session_id, "Blocker",
                          TASK_PRIORITY_HIGH, NULL, task1_id);
    task_store_create_task(g_store, g_session_id, "Blocked",
                          TASK_PRIORITY_MEDIUM, NULL, task2_id);
    task_store_add_dependency(g_store, task2_id, task1_id);

    // Remove dependency
    int result = task_store_remove_dependency(g_store, task2_id, task1_id);
    TEST_ASSERT_EQUAL(0, result);

    // Verify dependency was removed
    size_t count = 0;
    char** blockers = task_store_get_blockers(g_store, task2_id, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(blockers);
}

void test_task_store_is_blocked(void) {
    char task1_id[40];
    char task2_id[40];

    // Create two tasks and add dependency
    task_store_create_task(g_store, g_session_id, "Blocker",
                          TASK_PRIORITY_HIGH, NULL, task1_id);
    task_store_create_task(g_store, g_session_id, "Blocked",
                          TASK_PRIORITY_MEDIUM, NULL, task2_id);
    task_store_add_dependency(g_store, task2_id, task1_id);

    // task2 should be blocked (task1 is pending)
    TEST_ASSERT_EQUAL(1, task_store_is_blocked(g_store, task2_id));

    // Complete task1
    task_store_update_status(g_store, task1_id, TASK_STATUS_COMPLETED);

    // task2 should no longer be blocked
    TEST_ASSERT_EQUAL(0, task_store_is_blocked(g_store, task2_id));
}

void test_task_store_get_blocking(void) {
    char blocker_id[40];
    char blocked1_id[40];
    char blocked2_id[40];

    // Create tasks
    task_store_create_task(g_store, g_session_id, "Blocker",
                          TASK_PRIORITY_HIGH, NULL, blocker_id);
    task_store_create_task(g_store, g_session_id, "Blocked 1",
                          TASK_PRIORITY_MEDIUM, NULL, blocked1_id);
    task_store_create_task(g_store, g_session_id, "Blocked 2",
                          TASK_PRIORITY_LOW, NULL, blocked2_id);

    // Add dependencies
    task_store_add_dependency(g_store, blocked1_id, blocker_id);
    task_store_add_dependency(g_store, blocked2_id, blocker_id);

    // Get tasks blocked by blocker
    size_t count = 0;
    char** blocking = task_store_get_blocking(g_store, blocker_id, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(blocking);

    task_free_id_list(blocking, count);
}

void test_task_store_circular_dependency_prevention_self(void) {
    char task_id[40];

    task_store_create_task(g_store, g_session_id, "Self dependency test",
                          TASK_PRIORITY_MEDIUM, NULL, task_id);

    // Attempt to make task depend on itself - should fail
    int result = task_store_add_dependency(g_store, task_id, task_id);
    TEST_ASSERT_EQUAL(-2, result);
}

void test_task_store_circular_dependency_prevention_chain(void) {
    char task_a_id[40];
    char task_b_id[40];
    char task_c_id[40];

    // Create tasks
    task_store_create_task(g_store, g_session_id, "Task A",
                          TASK_PRIORITY_MEDIUM, NULL, task_a_id);
    task_store_create_task(g_store, g_session_id, "Task B",
                          TASK_PRIORITY_MEDIUM, NULL, task_b_id);
    task_store_create_task(g_store, g_session_id, "Task C",
                          TASK_PRIORITY_MEDIUM, NULL, task_c_id);

    // Create chain: A is blocked by B, B is blocked by C
    int result = task_store_add_dependency(g_store, task_a_id, task_b_id);
    TEST_ASSERT_EQUAL(0, result);

    result = task_store_add_dependency(g_store, task_b_id, task_c_id);
    TEST_ASSERT_EQUAL(0, result);

    // Attempt to create cycle: C blocked by A - should fail
    result = task_store_add_dependency(g_store, task_c_id, task_a_id);
    TEST_ASSERT_EQUAL(-2, result);
}

void test_task_store_cascade_delete_cleans_deps(void) {
    char task1_id[40];
    char task2_id[40];
    char task3_id[40];

    // Create tasks and dependencies
    task_store_create_task(g_store, g_session_id, "Task 1",
                          TASK_PRIORITY_MEDIUM, NULL, task1_id);
    task_store_create_task(g_store, g_session_id, "Task 2",
                          TASK_PRIORITY_MEDIUM, NULL, task2_id);
    task_store_create_task(g_store, g_session_id, "Task 3",
                          TASK_PRIORITY_MEDIUM, NULL, task3_id);

    // task2 blocked by task1, task3 blocked by task2
    task_store_add_dependency(g_store, task2_id, task1_id);
    task_store_add_dependency(g_store, task3_id, task2_id);

    // Delete task2 - should clean up both dependency records
    task_store_delete_task(g_store, task2_id);

    // task1 should have no blockers
    size_t count = 0;
    char** blocking = task_store_get_blocking(g_store, task1_id, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(blocking);

    // task3 should have no blockers
    char** blockers = task_store_get_blockers(g_store, task3_id, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(blockers);
}

// =============================================================================
// Query Tests
// =============================================================================

void test_task_store_list_by_session(void) {
    char session2_id[40];
    uuid_generate_v4(session2_id);

    char id1[40], id2[40], id3[40];

    // Create tasks in different sessions
    task_store_create_task(g_store, g_session_id, "Session 1 Task 1",
                          TASK_PRIORITY_MEDIUM, NULL, id1);
    task_store_create_task(g_store, g_session_id, "Session 1 Task 2",
                          TASK_PRIORITY_MEDIUM, NULL, id2);
    task_store_create_task(g_store, session2_id, "Session 2 Task 1",
                          TASK_PRIORITY_MEDIUM, NULL, id3);

    // List tasks for g_session_id
    size_t count = 0;
    Task** tasks = task_store_list_by_session(g_store, g_session_id, -1, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(tasks);
    task_free_list(tasks, count);

    // List tasks for session2_id
    tasks = task_store_list_by_session(g_store, session2_id, -1, &count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(tasks);
    task_free_list(tasks, count);
}

void test_task_store_list_by_session_with_status_filter(void) {
    char id1[40], id2[40], id3[40];

    // Create tasks with different statuses
    task_store_create_task(g_store, g_session_id, "Pending",
                          TASK_PRIORITY_MEDIUM, NULL, id1);
    task_store_create_task(g_store, g_session_id, "In Progress",
                          TASK_PRIORITY_MEDIUM, NULL, id2);
    task_store_create_task(g_store, g_session_id, "Completed",
                          TASK_PRIORITY_MEDIUM, NULL, id3);

    task_store_update_status(g_store, id2, TASK_STATUS_IN_PROGRESS);
    task_store_update_status(g_store, id3, TASK_STATUS_COMPLETED);

    // Filter by pending
    size_t count = 0;
    Task** tasks = task_store_list_by_session(g_store, g_session_id,
                                              TASK_STATUS_PENDING, &count);
    TEST_ASSERT_EQUAL(1, count);
    task_free_list(tasks, count);

    // Filter by completed
    tasks = task_store_list_by_session(g_store, g_session_id,
                                       TASK_STATUS_COMPLETED, &count);
    TEST_ASSERT_EQUAL(1, count);
    task_free_list(tasks, count);
}

void test_task_store_list_roots(void) {
    char parent_id[40], child_id[40], root2_id[40];

    // Create hierarchy
    task_store_create_task(g_store, g_session_id, "Root 1",
                          TASK_PRIORITY_MEDIUM, NULL, parent_id);
    task_store_create_task(g_store, g_session_id, "Child of Root 1",
                          TASK_PRIORITY_MEDIUM, parent_id, child_id);
    task_store_create_task(g_store, g_session_id, "Root 2",
                          TASK_PRIORITY_MEDIUM, NULL, root2_id);

    // List roots - should only return Root 1 and Root 2
    size_t count = 0;
    Task** roots = task_store_list_roots(g_store, g_session_id, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(roots);

    task_free_list(roots, count);
}

void test_task_store_list_ready(void) {
    char blocker_id[40], blocked_id[40], ready_id[40];

    // Create tasks
    task_store_create_task(g_store, g_session_id, "Blocker",
                          TASK_PRIORITY_HIGH, NULL, blocker_id);
    task_store_create_task(g_store, g_session_id, "Blocked",
                          TASK_PRIORITY_MEDIUM, NULL, blocked_id);
    task_store_create_task(g_store, g_session_id, "Ready",
                          TASK_PRIORITY_LOW, NULL, ready_id);

    // Add dependency
    task_store_add_dependency(g_store, blocked_id, blocker_id);

    // List ready tasks - should return "Blocker" and "Ready", not "Blocked"
    size_t count = 0;
    Task** ready = task_store_list_ready(g_store, g_session_id, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(ready);

    // Verify "Blocked" is not in the list
    int found_blocked = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(ready[i]->id, blocked_id) == 0) {
            found_blocked = 1;
        }
    }
    TEST_ASSERT_FALSE(found_blocked);

    task_free_list(ready, count);
}

void test_task_store_has_pending(void) {
    char id1[40], id2[40];

    // No tasks - should return 0
    TEST_ASSERT_EQUAL(0, task_store_has_pending(g_store, g_session_id));

    // Create pending task
    task_store_create_task(g_store, g_session_id, "Pending",
                          TASK_PRIORITY_MEDIUM, NULL, id1);
    TEST_ASSERT_EQUAL(1, task_store_has_pending(g_store, g_session_id));

    // Complete all tasks
    task_store_update_status(g_store, id1, TASK_STATUS_COMPLETED);
    TEST_ASSERT_EQUAL(0, task_store_has_pending(g_store, g_session_id));

    // Create in_progress task
    task_store_create_task(g_store, g_session_id, "In Progress",
                          TASK_PRIORITY_MEDIUM, NULL, id2);
    task_store_update_status(g_store, id2, TASK_STATUS_IN_PROGRESS);
    TEST_ASSERT_EQUAL(1, task_store_has_pending(g_store, g_session_id));
}

// =============================================================================
// Bulk Operations Tests
// =============================================================================

void test_task_store_replace_session_tasks(void) {
    char id1[40], id2[40];

    // Create initial tasks
    task_store_create_task(g_store, g_session_id, "Original 1",
                          TASK_PRIORITY_MEDIUM, NULL, id1);
    task_store_create_task(g_store, g_session_id, "Original 2",
                          TASK_PRIORITY_MEDIUM, NULL, id2);

    // Verify initial tasks exist
    size_t count = 0;
    Task** tasks = task_store_list_by_session(g_store, g_session_id, -1, &count);
    TEST_ASSERT_EQUAL(2, count);
    task_free_list(tasks, count);

    // Create replacement tasks array
    Task new_tasks[2];
    memset(&new_tasks, 0, sizeof(new_tasks));
    new_tasks[0].content = "Replacement 1";
    new_tasks[0].status = TASK_STATUS_PENDING;
    new_tasks[0].priority = TASK_PRIORITY_HIGH;
    new_tasks[1].content = "Replacement 2";
    new_tasks[1].status = TASK_STATUS_IN_PROGRESS;
    new_tasks[1].priority = TASK_PRIORITY_LOW;

    // Replace tasks
    int result = task_store_replace_session_tasks(g_store, g_session_id, new_tasks, 2);
    TEST_ASSERT_EQUAL(0, result);

    // Verify replacement
    tasks = task_store_list_by_session(g_store, g_session_id, -1, &count);
    TEST_ASSERT_EQUAL(2, count);

    // Original tasks should not exist
    Task* original = task_store_get_task(g_store, id1);
    TEST_ASSERT_NULL(original);

    task_free_list(tasks, count);
}

void test_task_store_replace_session_tasks_empty(void) {
    char id1[40];

    // Create initial task
    task_store_create_task(g_store, g_session_id, "Original",
                          TASK_PRIORITY_MEDIUM, NULL, id1);

    // Replace with empty list
    int result = task_store_replace_session_tasks(g_store, g_session_id, NULL, 0);
    TEST_ASSERT_EQUAL(0, result);

    // Verify all tasks deleted
    size_t count = 0;
    Task** tasks = task_store_list_by_session(g_store, g_session_id, -1, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(tasks);
}

// =============================================================================
// Status/Priority Conversion Tests
// =============================================================================

void test_task_status_conversion(void) {
    TEST_ASSERT_EQUAL_STRING("pending", task_status_to_string(TASK_STATUS_PENDING));
    TEST_ASSERT_EQUAL_STRING("in_progress", task_status_to_string(TASK_STATUS_IN_PROGRESS));
    TEST_ASSERT_EQUAL_STRING("completed", task_status_to_string(TASK_STATUS_COMPLETED));

    TEST_ASSERT_EQUAL(TASK_STATUS_PENDING, task_status_from_string("pending"));
    TEST_ASSERT_EQUAL(TASK_STATUS_IN_PROGRESS, task_status_from_string("in_progress"));
    TEST_ASSERT_EQUAL(TASK_STATUS_COMPLETED, task_status_from_string("completed"));
    TEST_ASSERT_EQUAL(TASK_STATUS_PENDING, task_status_from_string("unknown"));
    TEST_ASSERT_EQUAL(TASK_STATUS_PENDING, task_status_from_string(NULL));
}

void test_task_priority_conversion(void) {
    TEST_ASSERT_EQUAL_STRING("low", task_priority_to_string(TASK_PRIORITY_LOW));
    TEST_ASSERT_EQUAL_STRING("medium", task_priority_to_string(TASK_PRIORITY_MEDIUM));
    TEST_ASSERT_EQUAL_STRING("high", task_priority_to_string(TASK_PRIORITY_HIGH));

    TEST_ASSERT_EQUAL(TASK_PRIORITY_LOW, task_priority_from_string("low"));
    TEST_ASSERT_EQUAL(TASK_PRIORITY_MEDIUM, task_priority_from_string("medium"));
    TEST_ASSERT_EQUAL(TASK_PRIORITY_HIGH, task_priority_from_string("high"));
    TEST_ASSERT_EQUAL(TASK_PRIORITY_MEDIUM, task_priority_from_string("unknown"));
    TEST_ASSERT_EQUAL(TASK_PRIORITY_MEDIUM, task_priority_from_string(NULL));
}

// =============================================================================
// Memory Management Tests
// =============================================================================

void test_task_free_null(void) {
    // Should not crash
    task_free(NULL);
    task_free_list(NULL, 0);
    task_free_id_list(NULL, 0);
}

// =============================================================================
// Main
// =============================================================================

int main(void) {
    UNITY_BEGIN();

    // UUID Utils Tests
    RUN_TEST(test_uuid_generate_v4);
    RUN_TEST(test_uuid_is_valid);

    // Task Store Basic Tests
    RUN_TEST(test_task_store_create_destroy);
    RUN_TEST(test_task_store_multiple_instances);

    // CRUD Tests
    RUN_TEST(test_task_store_create_task);
    RUN_TEST(test_task_store_create_task_null_params);
    RUN_TEST(test_task_store_get_task);
    RUN_TEST(test_task_store_get_nonexistent_task);
    RUN_TEST(test_task_store_update_status);
    RUN_TEST(test_task_store_update_content);
    RUN_TEST(test_task_store_update_priority);
    RUN_TEST(test_task_store_delete_task);
    RUN_TEST(test_task_store_delete_nonexistent_task);

    // Parent/Child Tests
    RUN_TEST(test_task_store_create_subtask);
    RUN_TEST(test_task_store_get_children);
    RUN_TEST(test_task_store_get_subtree);
    RUN_TEST(test_task_store_delete_parent_cascades);
    RUN_TEST(test_task_store_set_parent);

    // Dependency Tests
    RUN_TEST(test_task_store_add_dependency);
    RUN_TEST(test_task_store_remove_dependency);
    RUN_TEST(test_task_store_is_blocked);
    RUN_TEST(test_task_store_get_blocking);
    RUN_TEST(test_task_store_circular_dependency_prevention_self);
    RUN_TEST(test_task_store_circular_dependency_prevention_chain);
    RUN_TEST(test_task_store_cascade_delete_cleans_deps);

    // Query Tests
    RUN_TEST(test_task_store_list_by_session);
    RUN_TEST(test_task_store_list_by_session_with_status_filter);
    RUN_TEST(test_task_store_list_roots);
    RUN_TEST(test_task_store_list_ready);
    RUN_TEST(test_task_store_has_pending);

    // Bulk Operations Tests
    RUN_TEST(test_task_store_replace_session_tasks);
    RUN_TEST(test_task_store_replace_session_tasks_empty);

    // Conversion Tests
    RUN_TEST(test_task_status_conversion);
    RUN_TEST(test_task_priority_conversion);

    // Memory Management Tests
    RUN_TEST(test_task_free_null);

    return UNITY_END();
}
