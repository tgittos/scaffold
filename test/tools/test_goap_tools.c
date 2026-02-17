#include "unity.h"
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

static const char *TEST_DB = "/tmp/test_goap_tools.db";
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
    goap_tools_set_services(svc);
}

void tearDown(void) {
    /* services_destroy will free stores via NULL checks */
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
    tc.arguments = strdup(args);
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

/* Create a goal directly via store, return goal_id */
static void create_test_goal(char *out_id) {
    goal_store_insert(gs, "Test goal", "Build something",
                      "{\"done\":true,\"tested\":true}", "test-q", out_id);
}

/* ========================================================================
 * goap_create_goal
 * ======================================================================== */

void test_create_goal(void) {
    ToolCall tc = make_tc("1", "goap_create_goal",
        "{\"name\":\"Build app\","
        "\"description\":\"Build a web application\","
        "\"goal_state\":{\"app_functional\":true,\"tests_passing\":true}}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(resp);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "success")));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(resp, "goal_id"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(resp, "queue_name"));
    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_create_goal_with_queue_name(void) {
    ToolCall tc = make_tc("2", "goap_create_goal",
        "{\"name\":\"G\",\"description\":\"D\","
        "\"goal_state\":{\"x\":true},\"queue_name\":\"my-queue\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL_STRING("my-queue",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "queue_name")));
    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_create_goal_missing_params(void) {
    ToolCall tc = make_tc("3", "goap_create_goal",
        "{\"name\":\"G\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_get_goal
 * ======================================================================== */

void test_get_goal(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("4", "goap_get_goal", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_get_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL_STRING("Test goal",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "name")));
    TEST_ASSERT_EQUAL_STRING("Build something",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "description")));
    TEST_ASSERT_EQUAL_STRING("planning",
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "status")));

    cJSON *goal_state = cJSON_GetObjectItem(resp, "goal_state");
    TEST_ASSERT_NOT_NULL(goal_state);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(goal_state, "done")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(goal_state, "tested")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_get_goal_not_found(void) {
    ToolCall tc = make_tc("5", "goap_get_goal",
        "{\"goal_id\":\"nonexistent\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_get_goal(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_create_actions
 * ======================================================================== */

void test_create_actions(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char args[1024];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Set up infra\",\"is_compound\":true,"
        "\"preconditions\":[],\"effects\":[\"infra_ready\"]},"
        "{\"description\":\"Build backend\",\"is_compound\":false,"
        "\"preconditions\":[\"infra_ready\"],\"effects\":[\"done\"],"
        "\"role\":\"implementation\"}"
        "]}", goal_id);

    ToolCall tc = make_tc("6", "goap_create_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "created")));

    cJSON *ids = cJSON_GetObjectItem(resp, "action_ids");
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(ids));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_create_actions_all_fail(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    /* Actions missing required "description" field should all fail */
    char args[512];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"effects\":[\"a\"],\"is_compound\":false,\"preconditions\":[]},"
        "{\"effects\":[\"b\"],\"is_compound\":false}"
        "]}", goal_id);

    ToolCall tc = make_tc("6b", "goap_create_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "success")));
    TEST_ASSERT_EQUAL(0, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "created")));
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "failed")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_create_actions_optional_effects(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    /* Actions without effects or preconditions should succeed with defaults */
    char args[512];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Minimal action\"},"
        "{\"description\":\"With preconditions only\",\"preconditions\":[\"x\"]}"
        "]}", goal_id);

    ToolCall tc = make_tc("6c", "goap_create_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "success")));
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "created")));

    /* Verify the first action was stored with default empty effects */
    cJSON *ids = cJSON_GetObjectItem(resp, "action_ids");
    const char *first_id = cJSON_GetStringValue(cJSON_GetArrayItem(ids, 0));
    Action *a = action_store_get(as, first_id);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("[]", a->effects);
    TEST_ASSERT_EQUAL_STRING("[]", a->preconditions);
    action_free(a);

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_create_actions_with_parent(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    /* Create parent compound action */
    char parent_id[40];
    action_store_insert(as, goal_id, NULL, "Phase 1",
                        "[]", "[\"phase1_done\"]", true, NULL, parent_id);

    /* Create children via tool */
    char args[1024];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Child 1\",\"is_compound\":false,"
        "\"preconditions\":[],\"effects\":[\"c1\"],"
        "\"parent_action_id\":\"%s\"},"
        "{\"description\":\"Child 2\",\"is_compound\":false,"
        "\"preconditions\":[\"c1\"],\"effects\":[\"phase1_done\"],"
        "\"parent_action_id\":\"%s\"}"
        "]}", goal_id, parent_id, parent_id);

    ToolCall tc = make_tc("7", "goap_create_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_create_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "created")));

    /* Verify children are linked via list_children */
    size_t child_count = 0;
    Action **children = action_store_list_children(as, parent_id, &child_count);
    TEST_ASSERT_EQUAL(2, (int)child_count);
    action_free_list(children, child_count);

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_list_actions
 * ======================================================================== */

void test_list_actions(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char _id1[40], _id2[40];
    action_store_insert(as, goal_id, NULL, "Action 1",
                        "[]", "[\"a\"]", false, "implementation", _id1);
    action_store_insert(as, goal_id, NULL, "Action 2",
                        "[\"a\"]", "[\"b\"]", false, "testing", _id2);

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("8", "goap_list_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_list_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_list_actions_filter_status(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char id1[40], id2[40];
    action_store_insert(as, goal_id, NULL, "Pending",
                        "[]", "[\"a\"]", false, NULL, id1);
    action_store_insert(as, goal_id, NULL, "Completed",
                        "[]", "[\"b\"]", false, NULL, id2);
    action_store_update_status(as, id2, ACTION_STATUS_COMPLETED, "done");

    char args[256];
    snprintf(args, sizeof(args),
             "{\"goal_id\":\"%s\",\"status\":\"completed\"}", goal_id);
    ToolCall tc = make_tc("9", "goap_list_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_list_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));

    cJSON *actions = cJSON_GetObjectItem(resp, "actions");
    cJSON *first = cJSON_GetArrayItem(actions, 0);
    TEST_ASSERT_EQUAL_STRING("Completed",
        cJSON_GetStringValue(cJSON_GetObjectItem(first, "description")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_list_actions_invalid_status(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char args[256];
    snprintf(args, sizeof(args),
             "{\"goal_id\":\"%s\",\"status\":\"in_progress\"}", goal_id);
    ToolCall tc = make_tc("9b", "goap_list_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_list_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    free_tc(&tc);
    free_tr(&tr);
}

void test_list_actions_by_parent(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char parent_id[40];
    action_store_insert(as, goal_id, NULL, "Parent",
                        "[]", "[\"x\"]", true, NULL, parent_id);
    char _ca[40], _cb[40], _cu[40];
    action_store_insert(as, goal_id, parent_id, "Child A",
                        "[]", "[\"a\"]", false, NULL, _ca);
    action_store_insert(as, goal_id, parent_id, "Child B",
                        "[]", "[\"b\"]", false, NULL, _cb);
    action_store_insert(as, goal_id, NULL, "Unrelated",
                        "[]", "[\"c\"]", false, NULL, _cu);

    char args[256];
    snprintf(args, sizeof(args),
             "{\"goal_id\":\"%s\",\"parent_action_id\":\"%s\"}",
             goal_id, parent_id);
    ToolCall tc = make_tc("10", "goap_list_actions", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_list_actions(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_update_action
 * ======================================================================== */

void test_update_action(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char action_id[40];
    action_store_insert(as, goal_id, NULL, "Do work",
                        "[]", "[\"x\"]", false, NULL, action_id);

    char args[256];
    snprintf(args, sizeof(args),
             "{\"action_id\":\"%s\",\"status\":\"completed\","
             "\"result\":\"Built the thing\"}", action_id);
    ToolCall tc = make_tc("11", "goap_update_action", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_update_action(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    /* Verify in store */
    Action *a = action_store_get(as, action_id);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL(ACTION_STATUS_COMPLETED, a->status);
    TEST_ASSERT_EQUAL_STRING("Built the thing", a->result);
    action_free(a);

    free_tc(&tc);
    free_tr(&tr);
}

void test_update_action_missing_params(void) {
    ToolCall tc = make_tc("12", "goap_update_action",
        "{\"action_id\":\"abc\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_update_action(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    free_tc(&tc);
    free_tr(&tr);
}

void test_update_action_invalid_status(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char action_id[40];
    action_store_insert(as, goal_id, NULL, "Work",
                        "[]", "[\"x\"]", false, NULL, action_id);

    char args[256];
    snprintf(args, sizeof(args),
             "{\"action_id\":\"%s\",\"status\":\"in_progress\"}", action_id);
    ToolCall tc = make_tc("12b", "goap_update_action", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_update_action(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(strstr(
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "error")),
        "Invalid status"));
    cJSON_Delete(resp);

    /* Verify action was not modified */
    Action *a = action_store_get(as, action_id);
    TEST_ASSERT_EQUAL(ACTION_STATUS_PENDING, a->status);
    action_free(a);

    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_dispatch_action (validation paths only — no fork/exec)
 * ======================================================================== */

void test_dispatch_compound_rejected(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char action_id[40];
    action_store_insert(as, goal_id, NULL, "Compound thing",
                        "[]", "[\"x\"]", true, NULL, action_id);

    char args[128];
    snprintf(args, sizeof(args), "{\"action_id\":\"%s\"}", action_id);
    ToolCall tc = make_tc("d1", "goap_dispatch_action", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_dispatch_action(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(strstr(
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "error")),
        "compound"));
    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_dispatch_non_pending_rejected(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char action_id[40];
    action_store_insert(as, goal_id, NULL, "Already done",
                        "[]", "[\"x\"]", false, NULL, action_id);
    action_store_update_status(as, action_id, ACTION_STATUS_COMPLETED, "done");

    char args[128];
    snprintf(args, sizeof(args), "{\"action_id\":\"%s\"}", action_id);
    ToolCall tc = make_tc("d2", "goap_dispatch_action", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_dispatch_action(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_NOT_NULL(strstr(
        cJSON_GetStringValue(cJSON_GetObjectItem(resp, "error")),
        "not pending"));
    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_dispatch_not_found(void) {
    ToolCall tc = make_tc("d3", "goap_dispatch_action",
        "{\"action_id\":\"nonexistent\"}");
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_dispatch_action(&tc, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_update_world_state
 * ======================================================================== */

void test_update_world_state(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char args[256];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"assertions\":{\"infra_ready\":true}}", goal_id);
    ToolCall tc = make_tc("13", "goap_update_world_state", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_update_world_state(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    cJSON *ws = cJSON_GetObjectItem(resp, "world_state");
    TEST_ASSERT_NOT_NULL(ws);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(ws, "infra_ready")));
    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_update_world_state_merge(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    /* First update */
    char args1[256];
    snprintf(args1, sizeof(args1),
        "{\"goal_id\":\"%s\",\"assertions\":{\"a\":true}}", goal_id);
    ToolCall tc1 = make_tc("14a", "goap_update_world_state", args1);
    ToolResult tr1 = {0};
    execute_goap_update_world_state(&tc1, &tr1);
    free_tc(&tc1);
    free_tr(&tr1);

    /* Second update — should merge, not replace */
    char args2[256];
    snprintf(args2, sizeof(args2),
        "{\"goal_id\":\"%s\",\"assertions\":{\"b\":true}}", goal_id);
    ToolCall tc2 = make_tc("14b", "goap_update_world_state", args2);
    ToolResult tr2 = {0};
    execute_goap_update_world_state(&tc2, &tr2);

    cJSON *resp = parse_result(&tr2);
    cJSON *ws = cJSON_GetObjectItem(resp, "world_state");
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(ws, "a")));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(ws, "b")));
    cJSON_Delete(resp);
    free_tc(&tc2);
    free_tr(&tr2);
}

/* ========================================================================
 * goap_check_complete
 * ======================================================================== */

void test_check_complete_incomplete(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    /* Goal state: {done: true, tested: true}, world state: {} */

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("15", "goap_check_complete", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_check_complete(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    TEST_ASSERT_EQUAL(0, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "satisfied")));
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "total")));

    cJSON *missing = cJSON_GetObjectItem(resp, "missing");
    TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(missing));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_check_complete_partial(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    goal_store_update_world_state(gs, goal_id, "{\"done\":true}");

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("16", "goap_check_complete", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_check_complete(&tc, &tr));

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "satisfied")));

    cJSON *missing = cJSON_GetObjectItem(resp, "missing");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(missing));
    TEST_ASSERT_EQUAL_STRING("tested",
        cJSON_GetStringValue(cJSON_GetArrayItem(missing, 0)));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_check_complete_done(void) {
    char goal_id[40];
    create_test_goal(goal_id);
    goal_store_update_world_state(gs, goal_id,
        "{\"done\":true,\"tested\":true}");

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("17", "goap_check_complete", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_check_complete(&tc, &tr));

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "satisfied")));
    /* No "missing" key when complete */
    TEST_ASSERT_NULL(cJSON_GetObjectItem(resp, "missing"));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * goap_get_action_results
 * ======================================================================== */

void test_get_action_results(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char id1[40], id2[40], id3[40];
    action_store_insert(as, goal_id, NULL, "Completed action",
                        "[]", "[\"a\"]", false, "implementation", id1);
    action_store_insert(as, goal_id, NULL, "Also completed",
                        "[]", "[\"b\"]", false, "testing", id2);
    action_store_insert(as, goal_id, NULL, "Still pending",
                        "[]", "[\"c\"]", false, NULL, id3);

    action_store_update_status(as, id1, ACTION_STATUS_COMPLETED, "Result one");
    action_store_update_status(as, id2, ACTION_STATUS_COMPLETED, "Result two");

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("18", "goap_get_action_results", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_get_action_results(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_get_action_results_with_filter(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char id1[40], id2[40];
    action_store_insert(as, goal_id, NULL, "Action A",
                        "[]", "[\"a\"]", false, NULL, id1);
    action_store_insert(as, goal_id, NULL, "Action B",
                        "[]", "[\"b\"]", false, NULL, id2);
    action_store_update_status(as, id1, ACTION_STATUS_COMPLETED, "R1");
    action_store_update_status(as, id2, ACTION_STATUS_COMPLETED, "R2");

    /* Filter to only id1 */
    char args[256];
    snprintf(args, sizeof(args),
             "{\"goal_id\":\"%s\",\"action_ids\":[\"%s\"]}",
             goal_id, id1);
    ToolCall tc = make_tc("19", "goap_get_action_results", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_get_action_results(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "count")));

    cJSON *results = cJSON_GetObjectItem(resp, "results");
    cJSON *first = cJSON_GetArrayItem(results, 0);
    TEST_ASSERT_EQUAL_STRING(id1,
        cJSON_GetStringValue(cJSON_GetObjectItem(first, "action_id")));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

void test_get_action_results_truncation(void) {
    char goal_id[40];
    create_test_goal(goal_id);

    char id1[40];
    action_store_insert(as, goal_id, NULL, "Big result",
                        "[]", "[\"x\"]", false, NULL, id1);

    /* Create a result > MAX_RESULT_PREVIEW (4000) chars */
    size_t big_len = 5000;
    char *big_result = malloc(big_len + 1);
    memset(big_result, 'A', big_len);
    big_result[big_len] = '\0';
    action_store_update_status(as, id1, ACTION_STATUS_COMPLETED, big_result);
    free(big_result);

    char args[128];
    snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc = make_tc("20", "goap_get_action_results", args);
    ToolResult tr = {0};

    TEST_ASSERT_EQUAL(0, execute_goap_get_action_results(&tc, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);

    cJSON *resp = parse_result(&tr);
    cJSON *results = cJSON_GetObjectItem(resp, "results");
    cJSON *first = cJSON_GetArrayItem(results, 0);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(first, "truncated")));

    const char *r = cJSON_GetStringValue(cJSON_GetObjectItem(first, "result"));
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_TRUE(strlen(r) < 5000);
    TEST_ASSERT_NOT_NULL(strstr(r, "...[truncated]"));

    cJSON_Delete(resp);
    free_tc(&tc);
    free_tr(&tr);
}

/* ========================================================================
 * Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* goap_create_goal */
    RUN_TEST(test_create_goal);
    RUN_TEST(test_create_goal_with_queue_name);
    RUN_TEST(test_create_goal_missing_params);

    /* goap_get_goal */
    RUN_TEST(test_get_goal);
    RUN_TEST(test_get_goal_not_found);

    /* goap_create_actions */
    RUN_TEST(test_create_actions);
    RUN_TEST(test_create_actions_all_fail);
    RUN_TEST(test_create_actions_optional_effects);
    RUN_TEST(test_create_actions_with_parent);

    /* goap_list_actions */
    RUN_TEST(test_list_actions);
    RUN_TEST(test_list_actions_filter_status);
    RUN_TEST(test_list_actions_invalid_status);
    RUN_TEST(test_list_actions_by_parent);

    /* goap_update_action */
    RUN_TEST(test_update_action);
    RUN_TEST(test_update_action_missing_params);
    RUN_TEST(test_update_action_invalid_status);

    /* goap_dispatch_action (validation only) */
    RUN_TEST(test_dispatch_compound_rejected);
    RUN_TEST(test_dispatch_non_pending_rejected);
    RUN_TEST(test_dispatch_not_found);

    /* goap_update_world_state */
    RUN_TEST(test_update_world_state);
    RUN_TEST(test_update_world_state_merge);

    /* goap_check_complete */
    RUN_TEST(test_check_complete_incomplete);
    RUN_TEST(test_check_complete_partial);
    RUN_TEST(test_check_complete_done);

    /* goap_get_action_results */
    RUN_TEST(test_get_action_results);
    RUN_TEST(test_get_action_results_with_filter);
    RUN_TEST(test_get_action_results_truncation);

    return UNITY_END();
}
