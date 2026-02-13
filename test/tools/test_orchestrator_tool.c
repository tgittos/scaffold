#include "unity.h"
#include "tools/orchestrator_tool.h"
#include "tools/goap_tools.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include "util/app_home.h"
#include "util/common_utils.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../test_fs_utils.h"

static const char *TEST_DB = "/tmp/test_orchestrator_tool.db";
static Services *svc = NULL;
static goal_store_t *gs = NULL;
static action_store_t *as = NULL;

void setUp(void) {
    app_home_init(NULL);
    unlink_sqlite_db(TEST_DB);
    gs = goal_store_create(TEST_DB);
    as = action_store_create(TEST_DB);
    svc = services_create_empty();
    svc->goal_store = gs;
    svc->action_store = as;
    orchestrator_tool_set_services(svc);
    goap_tools_set_services(svc);
}

void tearDown(void) {
    if (svc) {
        services_destroy(svc);
        svc = NULL;
        gs = NULL;
        as = NULL;
    }
    unlink_sqlite_db(TEST_DB);
    app_home_cleanup();
}

/* Helpers */

static ToolCall make_tc(const char *id, const char *name, const char *args) {
    ToolCall tc;
    tc.id = strdup(id);
    tc.name = strdup(name);
    tc.arguments = args ? strdup(args) : strdup("{}");
    return tc;
}

static void free_tc(ToolCall *tc) {
    free(tc->id);
    free(tc->name);
    free(tc->arguments);
}

static void free_tr(ToolResult *tr) {
    free(tr->tool_call_id);
    free(tr->result);
}

static cJSON *parse_result(ToolResult *tr) {
    return cJSON_Parse(tr->result);
}

static void create_test_goal(char *out_id) {
    goal_store_insert(gs, "Test goal", "Build something",
                      "{\"done\":true,\"tested\":true}", "test-q", out_id);
}

/* ========================================================================
 * execute_plan
 * ======================================================================== */

void test_execute_plan(void) {
    ToolCall tc = make_tc("1", "execute_plan",
        "{\"plan_text\":\"Build a Twitter clone with auth and timeline\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_execute_plan(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "success")));

    const char *inst = cJSON_GetStringValue(cJSON_GetObjectItem(resp, "instruction"));
    TEST_ASSERT_NOT_NULL(inst);
    TEST_ASSERT_NOT_NULL(strstr(inst, "DECOMPOSITION MODE"));
    TEST_ASSERT_NOT_NULL(strstr(inst, "Twitter clone"));
    TEST_ASSERT_NOT_NULL(strstr(inst, "goap_create_goal"));
    TEST_ASSERT_NOT_NULL(strstr(inst, "start_goal"));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_execute_plan_missing_param(void) {
    ToolCall tc = make_tc("2", "execute_plan", "{}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_execute_plan(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * list_goals
 * ======================================================================== */

void test_list_goals_empty(void) {
    ToolCall tc = make_tc("3", "list_goals", "{}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_list_goals(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(0, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));
    cJSON *goals = cJSON_GetObjectItem(resp, "goals");
    TEST_ASSERT_EQUAL(0, cJSON_GetArraySize(goals));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_list_goals_with_data(void) {
    char id1[40], id2[40];
    goal_store_insert(gs, "Goal A", "First goal",
                      "{\"a\":true}", "q1", id1);
    goal_store_insert(gs, "Goal B", "Second goal",
                      "{\"b\":true,\"c\":true}", "q2", id2);
    goal_store_update_world_state(gs, id2, "{\"b\":true}");

    ToolCall tc = make_tc("4", "list_goals", "{}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_list_goals(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));

    cJSON *goals = cJSON_GetObjectItem(resp, "goals");
    cJSON *g1 = cJSON_GetArrayItem(goals, 0);
    TEST_ASSERT_EQUAL_STRING("Goal A",
        cJSON_GetStringValue(cJSON_GetObjectItem(g1, "name")));
    TEST_ASSERT_EQUAL_STRING("0/1",
        cJSON_GetStringValue(cJSON_GetObjectItem(g1, "progress")));

    cJSON *g2 = cJSON_GetArrayItem(goals, 1);
    TEST_ASSERT_EQUAL_STRING("Goal B",
        cJSON_GetStringValue(cJSON_GetObjectItem(g2, "name")));
    TEST_ASSERT_EQUAL_STRING("1/2",
        cJSON_GetStringValue(cJSON_GetObjectItem(g2, "progress")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goal_status
 * ======================================================================== */

void test_goal_status(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    goal_store_update_world_state(gs, goal_id, "{\"done\":true}");
    goal_store_update_summary(gs, goal_id, "In progress");

    /* Create some actions */
    char a1[40], a2[40], a3[40];
    action_store_insert(as, goal_id, NULL, "Phase 1",
                        "[]", "[\"phase1\"]", true, NULL, a1);
    action_store_insert(as, goal_id, a1, "Subtask A",
                        "[]", "[\"done\"]", false, "implementation", a2);
    action_store_insert(as, goal_id, NULL, "Phase 2",
                        "[\"phase1\"]", "[\"tested\"]", true, NULL, a3);
    action_store_update_status(as, a2, ACTION_STATUS_COMPLETED, "Built it");

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("5", "goal_status", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goal_status(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_EQUAL_STRING("Test goal",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "name")));
    TEST_ASSERT_EQUAL_STRING("In progress",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "summary")));
    TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "assertions_satisfied")));
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "assertions_total")));

    /* Check action counts */
    cJSON *counts = cJSON_GetObjectItem(resp, "action_counts");
    TEST_ASSERT_NOT_NULL(counts);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(counts, "pending")));
    TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(counts, "completed")));

    /* Check action tree — should have 2 top-level actions */
    cJSON *tree = cJSON_GetObjectItem(resp, "action_tree");
    TEST_ASSERT_NOT_NULL(tree);
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(tree));

    /* First top-level action should be compound with children */
    cJSON *phase1 = cJSON_GetArrayItem(tree, 0);
    TEST_ASSERT_EQUAL_STRING("Phase 1",
        cJSON_GetStringValue(cJSON_GetObjectItem(phase1, "description")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(phase1, "is_compound")));
    cJSON *children = cJSON_GetObjectItem(phase1, "children");
    TEST_ASSERT_NOT_NULL(children);
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(children));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_goal_status_not_found(void) {
    ToolCall tc = make_tc("6", "goal_status",
        "{\"goal_id\":\"nonexistent\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goal_status(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * start_goal (validation only — supervisor spawn requires fork/exec)
 * ======================================================================== */

void test_start_goal_wrong_status(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    goal_store_update_status(gs, goal_id, GOAL_STATUS_ACTIVE);

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("7", "start_goal", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_start_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(strstr(
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "error")),
        "Cannot start"));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_start_goal_not_found(void) {
    ToolCall tc = make_tc("8", "start_goal",
        "{\"goal_id\":\"nonexistent\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_start_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * pause_goal
 * ======================================================================== */

void test_pause_goal_wrong_status(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    /* Goal starts in PLANNING status, can't pause */

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("9", "pause_goal", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_pause_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(strstr(
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "error")),
        "Cannot pause"));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_pause_goal_not_found(void) {
    ToolCall tc = make_tc("10", "pause_goal",
        "{\"goal_id\":\"nonexistent\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_pause_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * cancel_goal
 * ======================================================================== */

void test_cancel_goal(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    goal_store_update_status(gs, goal_id, GOAL_STATUS_ACTIVE);

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("11", "cancel_goal", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_cancel_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "success")));
    TEST_ASSERT_EQUAL_STRING("failed",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "status")));

    /* Verify in store */
    Goal *goal = goal_store_get(gs, goal_id);
    TEST_ASSERT_EQUAL(GOAL_STATUS_FAILED, goal->status);
    goal_free(goal);

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_cancel_goal_already_completed(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    goal_store_update_status(gs, goal_id, GOAL_STATUS_COMPLETED);

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("12", "cancel_goal", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_cancel_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(strstr(
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "error")),
        "terminal state"));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_cancel_goal_not_found(void) {
    ToolCall tc = make_tc("13", "cancel_goal",
        "{\"goal_id\":\"nonexistent\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_cancel_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* execute_plan */
    RUN_TEST(test_execute_plan);
    RUN_TEST(test_execute_plan_missing_param);

    /* list_goals */
    RUN_TEST(test_list_goals_empty);
    RUN_TEST(test_list_goals_with_data);

    /* goal_status */
    RUN_TEST(test_goal_status);
    RUN_TEST(test_goal_status_not_found);

    /* start_goal (validation only) */
    RUN_TEST(test_start_goal_wrong_status);
    RUN_TEST(test_start_goal_not_found);

    /* pause_goal */
    RUN_TEST(test_pause_goal_wrong_status);
    RUN_TEST(test_pause_goal_not_found);

    /* cancel_goal */
    RUN_TEST(test_cancel_goal);
    RUN_TEST(test_cancel_goal_already_completed);
    RUN_TEST(test_cancel_goal_not_found);

    return UNITY_END();
}
