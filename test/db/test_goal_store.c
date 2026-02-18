#include "unity.h"
#include "db/goal_store.h"
#include "util/uuid_utils.h"
#include "util/app_home.h"
#include "../test_fs_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_test_db_path[256];
static goal_store_t *g_store = NULL;

void setUp(void) {
    app_home_init(NULL);
    snprintf(g_test_db_path, sizeof(g_test_db_path), "/tmp/test_goals_%d.db", getpid());
    unlink_sqlite_db(g_test_db_path);
    g_store = goal_store_create(g_test_db_path);
}

void tearDown(void) {
    if (g_store != NULL) {
        goal_store_destroy(g_store);
        g_store = NULL;
    }
    unlink_sqlite_db(g_test_db_path);
    app_home_cleanup();
}

void test_goal_store_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(g_store);
}

void test_goal_store_insert(void) {
    char goal_id[40];
    int result = goal_store_insert(g_store, "Build app",
        "Build a web application", "{\"app_functional\": true}",
        "goal-queue-1", goal_id);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_TRUE(uuid_is_valid(goal_id));
}

void test_goal_store_insert_null_params(void) {
    char goal_id[40];
    TEST_ASSERT_EQUAL(-1, goal_store_insert(NULL, "name", "desc", "{}", "q", goal_id));
    TEST_ASSERT_EQUAL(-1, goal_store_insert(g_store, NULL, "desc", "{}", "q", goal_id));
    TEST_ASSERT_EQUAL(-1, goal_store_insert(g_store, "name", "desc", "{}", NULL, goal_id));
    TEST_ASSERT_EQUAL(-1, goal_store_insert(g_store, "name", "desc", "{}", "q", NULL));
}

void test_goal_store_insert_optional_nulls(void) {
    char goal_id[40];
    int result = goal_store_insert(g_store, "Minimal goal",
        NULL, NULL, "q1", goal_id);
    TEST_ASSERT_EQUAL(0, result);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    TEST_ASSERT_EQUAL_STRING("Minimal goal", goal->name);
    TEST_ASSERT_NULL(goal->description);
    TEST_ASSERT_EQUAL_STRING("{}", goal->goal_state);
    TEST_ASSERT_EQUAL_STRING("{}", goal->world_state);
    goal_free(goal);
}

void test_goal_store_get(void) {
    char goal_id[40];
    goal_store_insert(g_store, "Test Goal", "A test goal",
        "{\"tests_passing\": true}", "test-queue", goal_id);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    TEST_ASSERT_EQUAL_STRING(goal_id, goal->id);
    TEST_ASSERT_EQUAL_STRING("Test Goal", goal->name);
    TEST_ASSERT_EQUAL_STRING("A test goal", goal->description);
    TEST_ASSERT_EQUAL_STRING("{\"tests_passing\": true}", goal->goal_state);
    TEST_ASSERT_EQUAL_STRING("{}", goal->world_state);
    TEST_ASSERT_EQUAL(GOAL_STATUS_PLANNING, goal->status);
    TEST_ASSERT_EQUAL_STRING("test-queue", goal->queue_name);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    TEST_ASSERT_EQUAL(0, goal->supervisor_started_at);
    goal_free(goal);
}

void test_goal_store_get_nonexistent(void) {
    Goal *goal = goal_store_get(g_store, "nonexistent-uuid-1234-1234-123456789abc");
    TEST_ASSERT_NULL(goal);
}

void test_goal_store_update_status(void) {
    char goal_id[40];
    goal_store_insert(g_store, "Status Test", NULL, "{}", "q1", goal_id);

    int result = goal_store_update_status(g_store, goal_id, GOAL_STATUS_ACTIVE);
    TEST_ASSERT_EQUAL(0, result);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(GOAL_STATUS_ACTIVE, goal->status);
    goal_free(goal);

    result = goal_store_update_status(g_store, goal_id, GOAL_STATUS_COMPLETED);
    TEST_ASSERT_EQUAL(0, result);

    goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(GOAL_STATUS_COMPLETED, goal->status);
    goal_free(goal);
}

void test_goal_store_update_world_state(void) {
    char goal_id[40];
    goal_store_insert(g_store, "World State Test", NULL,
        "{\"a\": true, \"b\": true}", "q1", goal_id);

    int result = goal_store_update_world_state(g_store, goal_id,
        "{\"a\": true}");
    TEST_ASSERT_EQUAL(0, result);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL_STRING("{\"a\": true}", goal->world_state);
    goal_free(goal);
}

void test_goal_store_update_summary(void) {
    char goal_id[40];
    goal_store_insert(g_store, "Summary Test", NULL, "{}", "q1", goal_id);

    int result = goal_store_update_summary(g_store, goal_id, "Progress: 50%");
    TEST_ASSERT_EQUAL(0, result);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL_STRING("Progress: 50%", goal->summary);
    goal_free(goal);
}

void test_goal_store_update_supervisor(void) {
    char goal_id[40];
    goal_store_insert(g_store, "Supervisor Test", NULL, "{}", "q1", goal_id);

    int result = goal_store_update_supervisor(g_store, goal_id, 12345, 1700000000000LL);
    TEST_ASSERT_EQUAL(0, result);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(12345, goal->supervisor_pid);
    TEST_ASSERT_EQUAL(1700000000000LL, goal->supervisor_started_at);
    goal_free(goal);

    /* Clear supervisor */
    result = goal_store_update_supervisor(g_store, goal_id, 0, 0);
    TEST_ASSERT_EQUAL(0, result);

    goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    TEST_ASSERT_EQUAL(0, goal->supervisor_started_at);
    goal_free(goal);
}

void test_goal_store_list_all(void) {
    char id1[40], id2[40], id3[40];
    goal_store_insert(g_store, "Goal 1", NULL, "{}", "q1", id1);
    goal_store_insert(g_store, "Goal 2", NULL, "{}", "q2", id2);
    goal_store_insert(g_store, "Goal 3", NULL, "{}", "q3", id3);

    size_t count = 0;
    Goal **goals = goal_store_list_all(g_store, &count);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_NOT_NULL(goals);
    goal_free_list(goals, count);
}

void test_goal_store_list_all_empty(void) {
    size_t count = 0;
    Goal **goals = goal_store_list_all(g_store, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(goals);
}

void test_goal_store_list_by_status(void) {
    char id1[40], id2[40], id3[40];
    goal_store_insert(g_store, "Planning", NULL, "{}", "q1", id1);
    goal_store_insert(g_store, "Active", NULL, "{}", "q2", id2);
    goal_store_insert(g_store, "Also Active", NULL, "{}", "q3", id3);

    goal_store_update_status(g_store, id2, GOAL_STATUS_ACTIVE);
    goal_store_update_status(g_store, id3, GOAL_STATUS_ACTIVE);

    size_t count = 0;
    Goal **active = goal_store_list_by_status(g_store, GOAL_STATUS_ACTIVE, &count);
    TEST_ASSERT_EQUAL(2, count);
    TEST_ASSERT_NOT_NULL(active);
    goal_free_list(active, count);

    Goal **planning = goal_store_list_by_status(g_store, GOAL_STATUS_PLANNING, &count);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(planning);
    TEST_ASSERT_EQUAL_STRING("Planning", planning[0]->name);
    goal_free_list(planning, count);
}

void test_goal_store_update_nonexistent(void) {
    TEST_ASSERT_EQUAL(-1, goal_store_update_status(g_store,
        "nonexistent-uuid-1234-1234-123456789abc", GOAL_STATUS_ACTIVE));
    TEST_ASSERT_EQUAL(-1, goal_store_update_world_state(g_store,
        "nonexistent-uuid-1234-1234-123456789abc", "{}"));
    TEST_ASSERT_EQUAL(-1, goal_store_update_summary(g_store,
        "nonexistent-uuid-1234-1234-123456789abc", "summary"));
    TEST_ASSERT_EQUAL(-1, goal_store_update_supervisor(g_store,
        "nonexistent-uuid-1234-1234-123456789abc", 123, 0));
}

void test_goal_status_conversion(void) {
    TEST_ASSERT_EQUAL_STRING("planning", goal_status_to_string(GOAL_STATUS_PLANNING));
    TEST_ASSERT_EQUAL_STRING("active", goal_status_to_string(GOAL_STATUS_ACTIVE));
    TEST_ASSERT_EQUAL_STRING("paused", goal_status_to_string(GOAL_STATUS_PAUSED));
    TEST_ASSERT_EQUAL_STRING("completed", goal_status_to_string(GOAL_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL_STRING("failed", goal_status_to_string(GOAL_STATUS_FAILED));

    TEST_ASSERT_EQUAL(GOAL_STATUS_PLANNING, goal_status_from_string("planning"));
    TEST_ASSERT_EQUAL(GOAL_STATUS_ACTIVE, goal_status_from_string("active"));
    TEST_ASSERT_EQUAL(GOAL_STATUS_PAUSED, goal_status_from_string("paused"));
    TEST_ASSERT_EQUAL(GOAL_STATUS_COMPLETED, goal_status_from_string("completed"));
    TEST_ASSERT_EQUAL(GOAL_STATUS_FAILED, goal_status_from_string("failed"));
    TEST_ASSERT_EQUAL(GOAL_STATUS_PLANNING, goal_status_from_string("unknown"));
    TEST_ASSERT_EQUAL(GOAL_STATUS_PLANNING, goal_status_from_string(NULL));
}

void test_goal_store_has_active_goals_empty(void) {
    TEST_ASSERT_EQUAL(0, goal_store_has_active_goals(g_store));
}

void test_goal_store_has_active_goals_returns_1(void) {
    char id[40];
    goal_store_insert(g_store, "Active Goal", NULL, "{}", "q1", id);
    goal_store_update_status(g_store, id, GOAL_STATUS_ACTIVE);

    TEST_ASSERT_EQUAL(1, goal_store_has_active_goals(g_store));
}

void test_goal_store_has_active_goals_after_completed(void) {
    char id[40];
    goal_store_insert(g_store, "Will Complete", NULL, "{}", "q1", id);
    goal_store_update_status(g_store, id, GOAL_STATUS_ACTIVE);
    TEST_ASSERT_EQUAL(1, goal_store_has_active_goals(g_store));

    goal_store_update_status(g_store, id, GOAL_STATUS_COMPLETED);
    TEST_ASSERT_EQUAL(0, goal_store_has_active_goals(g_store));
}

void test_goal_store_has_active_goals_null(void) {
    TEST_ASSERT_EQUAL(-1, goal_store_has_active_goals(NULL));
}

void test_goal_free_null(void) {
    goal_free(NULL);
    goal_free_list(NULL, 0);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_goal_store_create_destroy);
    RUN_TEST(test_goal_store_insert);
    RUN_TEST(test_goal_store_insert_null_params);
    RUN_TEST(test_goal_store_insert_optional_nulls);
    RUN_TEST(test_goal_store_get);
    RUN_TEST(test_goal_store_get_nonexistent);
    RUN_TEST(test_goal_store_update_status);
    RUN_TEST(test_goal_store_update_world_state);
    RUN_TEST(test_goal_store_update_summary);
    RUN_TEST(test_goal_store_update_supervisor);
    RUN_TEST(test_goal_store_list_all);
    RUN_TEST(test_goal_store_list_all_empty);
    RUN_TEST(test_goal_store_list_by_status);
    RUN_TEST(test_goal_store_update_nonexistent);
    RUN_TEST(test_goal_status_conversion);
    RUN_TEST(test_goal_store_has_active_goals_empty);
    RUN_TEST(test_goal_store_has_active_goals_returns_1);
    RUN_TEST(test_goal_store_has_active_goals_after_completed);
    RUN_TEST(test_goal_store_has_active_goals_null);
    RUN_TEST(test_goal_free_null);

    return UNITY_END();
}
