#include "../../test/unity/unity.h"
#include "ui/goal_commands.h"
#include "agent/session.h"
#include "services/services.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../test_fs_utils.h"

static AgentSession g_session;
static Services *g_services;
static const char *TEST_DB = "/tmp/test_goal_commands.db";

static void redirect_stdout(void) {
    freopen("/dev/null", "w", stdout);
}

static void restore_stdout(void) {
    freopen("/dev/tty", "w", stdout);
}

void setUp(void) {
    unlink_sqlite_db(TEST_DB);
    memset(&g_session, 0, sizeof(g_session));

    g_services = services_create_empty();
    g_services->goal_store = goal_store_create(TEST_DB);
    g_services->action_store = action_store_create(TEST_DB);
    g_session.services = g_services;

    redirect_stdout();
}

void tearDown(void) {
    restore_stdout();
    if (g_services) {
        if (g_services->goal_store) goal_store_destroy(g_services->goal_store);
        if (g_services->action_store) action_store_destroy(g_services->action_store);
        g_services->goal_store = NULL;
        g_services->action_store = NULL;
        services_destroy(g_services);
        g_services = NULL;
    }
    g_session.services = NULL;
    unlink_sqlite_db(TEST_DB);
}

void test_goals_list_empty(void) {
    int result = process_goals_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_list_explicit(void) {
    int result = process_goals_command("list", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_help(void) {
    int result = process_goals_command("help", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_null_session(void) {
    int result = process_goals_command("", NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_goals_null_args(void) {
    int result = process_goals_command(NULL, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_list_with_goals(void) {
    char id[40];
    int rc = goal_store_insert(g_services->goal_store, "Test Goal",
                               "A test goal description",
                               "{\"task_done\": true, \"reviewed\": true}",
                               "test_queue", id);
    TEST_ASSERT_EQUAL_INT(0, rc);

    int result = process_goals_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_by_id(void) {
    char id[40];
    goal_store_insert(g_services->goal_store, "Show Goal",
                      "A goal to show",
                      "{\"built\": true}",
                      "show_queue", id);

    int result = process_goals_command(id, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_by_prefix(void) {
    char id[40];
    goal_store_insert(g_services->goal_store, "Prefix Goal",
                      "A goal to prefix-match",
                      "{\"done\": true}",
                      "prefix_queue", id);

    char prefix[9];
    snprintf(prefix, sizeof(prefix), "%.8s", id);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "show %s", prefix);
    int result = process_goals_command(cmd, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_not_found(void) {
    int result = process_goals_command("show deadbeef", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_no_id(void) {
    int result = process_goals_command("show ", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_with_world_state(void) {
    char id[40];
    goal_store_insert(g_services->goal_store, "WS Goal",
                      "Goal with world state",
                      "{\"alpha\": true, \"beta\": true}",
                      "ws_queue", id);
    goal_store_update_world_state(g_services->goal_store, id,
                                  "{\"alpha\": true, \"beta\": false}");

    int result = process_goals_command(id, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_with_actions(void) {
    char goal_id[40];
    goal_store_insert(g_services->goal_store, "Action Goal",
                      "Goal with actions",
                      "{\"done\": true}",
                      "action_queue", goal_id);

    /* Insert a compound top-level action */
    char compound_id[40];
    action_store_insert(g_services->action_store, goal_id, NULL,
                        "Phase 1: Setup",
                        "[]", "[\"setup_done\"]",
                        true, "implementation", compound_id);

    /* Insert a primitive child */
    char child_id[40];
    action_store_insert(g_services->action_store, goal_id, compound_id,
                        "Create project structure",
                        "[]", "[\"setup_done\"]",
                        false, "implementation", child_id);

    /* Insert another top-level primitive */
    char prim_id[40];
    action_store_insert(g_services->action_store, goal_id, NULL,
                        "Final review",
                        "[\"setup_done\"]", "[\"done\"]",
                        false, "code_review", prim_id);

    int result = process_goals_command(goal_id, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_show_with_completed_actions(void) {
    char goal_id[40];
    goal_store_insert(g_services->goal_store, "Mixed Goal",
                      "Goal with mixed action statuses",
                      "{\"built\": true, \"tested\": true}",
                      "mixed_queue", goal_id);

    char a1[40], a2[40];
    action_store_insert(g_services->action_store, goal_id, NULL,
                        "Build feature", "[]", "[\"built\"]",
                        false, "implementation", a1);
    action_store_insert(g_services->action_store, goal_id, NULL,
                        "Test feature", "[\"built\"]", "[\"tested\"]",
                        false, "testing", a2);

    action_store_update_status(g_services->action_store, a1,
                               ACTION_STATUS_COMPLETED, "Built successfully");

    int result = process_goals_command(goal_id, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_goals_no_store(void) {
    /* Remove goal store from services */
    goal_store_destroy(g_services->goal_store);
    g_services->goal_store = NULL;

    int result = process_goals_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_goals_list_empty);
    RUN_TEST(test_goals_list_explicit);
    RUN_TEST(test_goals_help);
    RUN_TEST(test_goals_null_session);
    RUN_TEST(test_goals_null_args);
    RUN_TEST(test_goals_list_with_goals);
    RUN_TEST(test_goals_show_by_id);
    RUN_TEST(test_goals_show_by_prefix);
    RUN_TEST(test_goals_show_not_found);
    RUN_TEST(test_goals_show_no_id);
    RUN_TEST(test_goals_show_with_world_state);
    RUN_TEST(test_goals_show_with_actions);
    RUN_TEST(test_goals_show_with_completed_actions);
    RUN_TEST(test_goals_no_store);

    return UNITY_END();
}
