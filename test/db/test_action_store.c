#include "unity.h"
#include "db/action_store.h"
#include "util/uuid_utils.h"
#include "util/app_home.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TEST_DB_PATH = "/tmp/test_actions.db";
static action_store_t *g_store = NULL;
static char g_goal_id[40];

void setUp(void) {
    app_home_init(NULL);
    unlink(TEST_DB_PATH);
    g_store = action_store_create(TEST_DB_PATH);
    uuid_generate_v4(g_goal_id);
}

void tearDown(void) {
    if (g_store != NULL) {
        action_store_destroy(g_store);
        g_store = NULL;
    }
    unlink(TEST_DB_PATH);
    app_home_cleanup();
}

void test_action_store_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(g_store);
}

void test_action_store_insert_primitive(void) {
    char action_id[40];
    int result = action_store_insert(g_store, g_goal_id, NULL,
        "Implement auth endpoints",
        "[\"database_schema_exists\"]",
        "[\"auth_endpoints_functional\"]",
        false, "implementation", action_id);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(uuid_is_valid(action_id));
}

void test_action_store_insert_compound(void) {
    char action_id[40];
    int result = action_store_insert(g_store, g_goal_id, NULL,
        "Build backend",
        "[]",
        "[\"backend_complete\"]",
        true, NULL, action_id);
    TEST_ASSERT_EQUAL(0, result);

    Action *action = action_store_get(g_store, action_id);
    TEST_ASSERT_NOT_NULL(action);
    TEST_ASSERT_TRUE(action->is_compound);
    TEST_ASSERT_EQUAL_STRING("implementation", action->role);
    action_free(action);
}

void test_action_store_insert_null_params(void) {
    char action_id[40];
    TEST_ASSERT_EQUAL(-1, action_store_insert(NULL, g_goal_id, NULL, "desc", "[]", "[]", false, NULL, action_id));
    TEST_ASSERT_EQUAL(-1, action_store_insert(g_store, NULL, NULL, "desc", "[]", "[]", false, NULL, action_id));
    TEST_ASSERT_EQUAL(-1, action_store_insert(g_store, g_goal_id, NULL, NULL, "[]", "[]", false, NULL, action_id));
    TEST_ASSERT_EQUAL(-1, action_store_insert(g_store, g_goal_id, NULL, "desc", "[]", "[]", false, NULL, NULL));
}

void test_action_store_insert_optional_nulls(void) {
    char action_id[40];
    int result = action_store_insert(g_store, g_goal_id, NULL,
        "Minimal action", NULL, NULL, false, NULL, action_id);
    TEST_ASSERT_EQUAL(0, result);

    Action *action = action_store_get(g_store, action_id);
    TEST_ASSERT_NOT_NULL(action);
    TEST_ASSERT_EQUAL_STRING("[]", action->preconditions);
    TEST_ASSERT_EQUAL_STRING("[]", action->effects);
    TEST_ASSERT_EQUAL_STRING("implementation", action->role);
    TEST_ASSERT_EQUAL_STRING("", action->parent_action_id);
    action_free(action);
}

void test_action_store_get(void) {
    char action_id[40];
    action_store_insert(g_store, g_goal_id, NULL,
        "Create database schema",
        "[]",
        "[\"database_schema_exists\"]",
        false, "implementation", action_id);

    Action *action = action_store_get(g_store, action_id);
    TEST_ASSERT_NOT_NULL(action);
    TEST_ASSERT_EQUAL_STRING(action_id, action->id);
    TEST_ASSERT_EQUAL_STRING(g_goal_id, action->goal_id);
    TEST_ASSERT_EQUAL_STRING("Create database schema", action->description);
    TEST_ASSERT_EQUAL_STRING("[]", action->preconditions);
    TEST_ASSERT_EQUAL_STRING("[\"database_schema_exists\"]", action->effects);
    TEST_ASSERT_FALSE(action->is_compound);
    TEST_ASSERT_EQUAL(ACTION_STATUS_PENDING, action->status);
    TEST_ASSERT_EQUAL_STRING("implementation", action->role);
    TEST_ASSERT_NULL(action->result);
    TEST_ASSERT_EQUAL(0, action->attempt_count);
    action_free(action);
}

void test_action_store_get_nonexistent(void) {
    Action *action = action_store_get(g_store, "nonexistent-uuid-1234-1234-123456789abc");
    TEST_ASSERT_NULL(action);
}

void test_action_store_update_status(void) {
    char action_id[40];
    action_store_insert(g_store, g_goal_id, NULL, "Test action",
        "[]", "[]", false, NULL, action_id);

    int result = action_store_update_status(g_store, action_id,
        ACTION_STATUS_RUNNING, NULL);
    TEST_ASSERT_EQUAL(0, result);

    Action *action = action_store_get(g_store, action_id);
    TEST_ASSERT_EQUAL(ACTION_STATUS_RUNNING, action->status);
    TEST_ASSERT_NULL(action->result);
    action_free(action);

    result = action_store_update_status(g_store, action_id,
        ACTION_STATUS_COMPLETED, "Task completed successfully");
    TEST_ASSERT_EQUAL(0, result);

    action = action_store_get(g_store, action_id);
    TEST_ASSERT_EQUAL(ACTION_STATUS_COMPLETED, action->status);
    TEST_ASSERT_EQUAL_STRING("Task completed successfully", action->result);
    action_free(action);
}

void test_action_store_parent_child(void) {
    char parent_id[40], child1_id[40], child2_id[40];

    action_store_insert(g_store, g_goal_id, NULL,
        "Build backend", "[]", "[\"backend_complete\"]",
        true, NULL, parent_id);

    action_store_insert(g_store, g_goal_id, parent_id,
        "Create schema", "[]", "[\"schema_exists\"]",
        false, "implementation", child1_id);

    action_store_insert(g_store, g_goal_id, parent_id,
        "Implement API", "[\"schema_exists\"]", "[\"api_ready\"]",
        false, "implementation", child2_id);

    /* Verify parent link */
    Action *child = action_store_get(g_store, child1_id);
    TEST_ASSERT_EQUAL_STRING(parent_id, child->parent_action_id);
    action_free(child);

    /* List children */
    size_t count = 0;
    Action **children = action_store_list_children(g_store, parent_id, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(children);
    action_free_list(children, count);
}

void test_action_store_list_by_goal(void) {
    char id1[40], id2[40], id3[40];
    char other_goal[40];
    uuid_generate_v4(other_goal);

    action_store_insert(g_store, g_goal_id, NULL, "Action 1", "[]", "[]", false, NULL, id1);
    action_store_insert(g_store, g_goal_id, NULL, "Action 2", "[]", "[]", false, NULL, id2);
    action_store_insert(g_store, other_goal, NULL, "Other goal action", "[]", "[]", false, NULL, id3);

    size_t count = 0;
    Action **actions = action_store_list_by_goal(g_store, g_goal_id, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(actions);
    action_free_list(actions, count);
}

void test_action_store_list_ready_no_preconditions(void) {
    char id1[40], id2[40];
    action_store_insert(g_store, g_goal_id, NULL, "No preconditions",
        "[]", "[\"a\"]", false, NULL, id1);
    action_store_insert(g_store, g_goal_id, NULL, "Also no preconditions",
        NULL, "[\"b\"]", false, NULL, id2);

    size_t count = 0;
    Action **ready = action_store_list_ready(g_store, g_goal_id, "{}", &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(ready);
    action_free_list(ready, count);
}

void test_action_store_list_ready_with_preconditions(void) {
    char id1[40], id2[40], id3[40];

    /* Always ready (no preconditions) */
    action_store_insert(g_store, g_goal_id, NULL, "Setup",
        "[]", "[\"project_init\"]", false, NULL, id1);

    /* Needs project_init */
    action_store_insert(g_store, g_goal_id, NULL, "Build",
        "[\"project_init\"]", "[\"built\"]", false, NULL, id2);

    /* Needs both project_init and built */
    action_store_insert(g_store, g_goal_id, NULL, "Deploy",
        "[\"project_init\", \"built\"]", "[\"deployed\"]", false, NULL, id3);

    /* Empty world state: only Setup is ready */
    size_t count = 0;
    Action **ready = action_store_list_ready(g_store, g_goal_id, "{}", &count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("Setup", ready[0]->description);
    action_free_list(ready, count);

    /* project_init true: Setup and Build are ready */
    ready = action_store_list_ready(g_store, g_goal_id,
        "{\"project_init\": true}", &count);
    TEST_ASSERT_EQUAL(2, count);
    action_free_list(ready, count);

    /* Both true: all three ready */
    ready = action_store_list_ready(g_store, g_goal_id,
        "{\"project_init\": true, \"built\": true}", &count);
    TEST_ASSERT_EQUAL(3, count);
    action_free_list(ready, count);
}

void test_action_store_list_ready_excludes_non_pending(void) {
    char id1[40], id2[40];

    action_store_insert(g_store, g_goal_id, NULL, "Running action",
        "[]", "[\"a\"]", false, NULL, id1);
    action_store_insert(g_store, g_goal_id, NULL, "Pending action",
        "[]", "[\"b\"]", false, NULL, id2);

    action_store_update_status(g_store, id1, ACTION_STATUS_RUNNING, NULL);

    size_t count = 0;
    Action **ready = action_store_list_ready(g_store, g_goal_id, "{}", &count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL_STRING("Pending action", ready[0]->description);
    action_free_list(ready, count);
}

void test_action_store_list_ready_false_in_world_state(void) {
    char id1[40];
    action_store_insert(g_store, g_goal_id, NULL, "Needs true",
        "[\"thing\"]", "[\"done\"]", false, NULL, id1);

    /* Key exists but is false */
    size_t count = 0;
    Action **ready = action_store_list_ready(g_store, g_goal_id,
        "{\"thing\": false}", &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(ready);
}

void test_action_store_count_by_status(void) {
    char id1[40], id2[40], id3[40];

    action_store_insert(g_store, g_goal_id, NULL, "A", "[]", "[]", false, NULL, id1);
    action_store_insert(g_store, g_goal_id, NULL, "B", "[]", "[]", false, NULL, id2);
    action_store_insert(g_store, g_goal_id, NULL, "C", "[]", "[]", false, NULL, id3);

    TEST_ASSERT_EQUAL(3, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_PENDING));
    TEST_ASSERT_EQUAL(0, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_RUNNING));

    action_store_update_status(g_store, id1, ACTION_STATUS_RUNNING, NULL);
    action_store_update_status(g_store, id2, ACTION_STATUS_COMPLETED, "done");

    TEST_ASSERT_EQUAL(1, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_PENDING));
    TEST_ASSERT_EQUAL(1, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_RUNNING));
    TEST_ASSERT_EQUAL(1, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_COMPLETED));
}

void test_action_store_skip_pending(void) {
    char id1[40], id2[40], id3[40];

    action_store_insert(g_store, g_goal_id, NULL, "A", "[]", "[]", false, NULL, id1);
    action_store_insert(g_store, g_goal_id, NULL, "B", "[]", "[]", false, NULL, id2);
    action_store_insert(g_store, g_goal_id, NULL, "C", "[]", "[]", false, NULL, id3);

    /* Mark one as running first */
    action_store_update_status(g_store, id1, ACTION_STATUS_RUNNING, NULL);

    /* Skip all pending — should skip B and C but not A (running) */
    int skipped = action_store_skip_pending(g_store, g_goal_id);
    TEST_ASSERT_EQUAL(2, skipped);

    TEST_ASSERT_EQUAL(0, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_PENDING));
    TEST_ASSERT_EQUAL(1, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_RUNNING));
    TEST_ASSERT_EQUAL(2, action_store_count_by_status(g_store, g_goal_id, ACTION_STATUS_SKIPPED));
}

void test_action_store_update_nonexistent(void) {
    TEST_ASSERT_EQUAL(-1, action_store_update_status(g_store,
        "nonexistent-uuid-1234-1234-123456789abc", ACTION_STATUS_RUNNING, NULL));
}

void test_action_store_list_ready_null_world_state(void) {
    char id1[40];
    action_store_insert(g_store, g_goal_id, NULL, "No preconditions",
        "[]", "[\"a\"]", false, NULL, id1);

    size_t count = 0;
    Action **ready = action_store_list_ready(g_store, g_goal_id, NULL, &count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(ready);
    action_free_list(ready, count);
}

void test_action_store_list_children_empty(void) {
    char parent_id[40];
    action_store_insert(g_store, g_goal_id, NULL, "Parent", "[]", "[]", true, NULL, parent_id);

    size_t count = 0;
    Action **children = action_store_list_children(g_store, parent_id, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(children);
}

void test_action_status_conversion(void) {
    TEST_ASSERT_EQUAL_STRING("pending", action_status_to_string(ACTION_STATUS_PENDING));
    TEST_ASSERT_EQUAL_STRING("running", action_status_to_string(ACTION_STATUS_RUNNING));
    TEST_ASSERT_EQUAL_STRING("completed", action_status_to_string(ACTION_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL_STRING("failed", action_status_to_string(ACTION_STATUS_FAILED));
    TEST_ASSERT_EQUAL_STRING("skipped", action_status_to_string(ACTION_STATUS_SKIPPED));

    TEST_ASSERT_EQUAL(ACTION_STATUS_PENDING, action_status_from_string("pending"));
    TEST_ASSERT_EQUAL(ACTION_STATUS_RUNNING, action_status_from_string("running"));
    TEST_ASSERT_EQUAL(ACTION_STATUS_COMPLETED, action_status_from_string("completed"));
    TEST_ASSERT_EQUAL(ACTION_STATUS_FAILED, action_status_from_string("failed"));
    TEST_ASSERT_EQUAL(ACTION_STATUS_SKIPPED, action_status_from_string("skipped"));
    TEST_ASSERT_EQUAL(ACTION_STATUS_PENDING, action_status_from_string("unknown"));
    TEST_ASSERT_EQUAL(ACTION_STATUS_PENDING, action_status_from_string(NULL));
}

void test_action_free_null(void) {
    action_free(NULL);
    action_free_list(NULL, 0);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_action_store_create_destroy);
    RUN_TEST(test_action_store_insert_primitive);
    RUN_TEST(test_action_store_insert_compound);
    RUN_TEST(test_action_store_insert_null_params);
    RUN_TEST(test_action_store_insert_optional_nulls);
    RUN_TEST(test_action_store_get);
    RUN_TEST(test_action_store_get_nonexistent);
    RUN_TEST(test_action_store_update_status);
    RUN_TEST(test_action_store_parent_child);
    RUN_TEST(test_action_store_list_by_goal);
    RUN_TEST(test_action_store_list_ready_no_preconditions);
    RUN_TEST(test_action_store_list_ready_with_preconditions);
    RUN_TEST(test_action_store_list_ready_excludes_non_pending);
    RUN_TEST(test_action_store_list_ready_false_in_world_state);
    RUN_TEST(test_action_store_count_by_status);
    RUN_TEST(test_action_store_skip_pending);
    RUN_TEST(test_action_store_update_nonexistent);
    RUN_TEST(test_action_store_list_ready_null_world_state);
    RUN_TEST(test_action_store_list_children_empty);
    RUN_TEST(test_action_status_conversion);
    RUN_TEST(test_action_free_null);

    return UNITY_END();
}
