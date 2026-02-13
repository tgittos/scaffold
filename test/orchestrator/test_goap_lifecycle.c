#include "unity.h"
#include "tools/goap_tools.h"
#include "tools/orchestrator_tool.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include "util/app_home.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Integration test: Full GOAP lifecycle through the tool API.
 *
 * Simulates the supervisor's perspective — creating goals, decomposing
 * compound actions, dispatching primitives, updating world state, and
 * checking goal completion — all via the GOAP tool functions.
 *
 * This exercises the complete data flow across goal_store, action_store,
 * and the GOAP tool layer without requiring an LLM or fork/exec.
 */

static const char *TEST_DB = "/tmp/test_goap_lifecycle.db";
static Services *svc = NULL;
static goal_store_t *gs = NULL;
static action_store_t *as = NULL;

void setUp(void) {
    app_home_init(NULL);
    unlink(TEST_DB);
    gs = goal_store_create(TEST_DB);
    as = action_store_create(TEST_DB);
    svc = services_create_empty();
    svc->goal_store = gs;
    svc->action_store = as;
    goap_tools_set_services(svc);
    orchestrator_tool_set_services(svc);
}

void tearDown(void) {
    if (svc) {
        services_destroy(svc);
        svc = NULL;
        gs = NULL;
        as = NULL;
    }
    unlink(TEST_DB);
    app_home_cleanup();
}

/* ========================================================================
 * Helpers
 * ======================================================================== */

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

static void extract_id(ToolResult *tr, const char *key, char *out, size_t out_len) {
    cJSON *resp = parse_result(tr);
    const char *val = cJSON_GetStringValue(cJSON_GetObjectItem(resp, key));
    if (val) snprintf(out, out_len, "%s", val);
    else out[0] = '\0';
    cJSON_Delete(resp);
}

static void extract_action_ids(ToolResult *tr, char ids[][40], int *count) {
    cJSON *resp = parse_result(tr);
    cJSON *arr = cJSON_GetObjectItem(resp, "action_ids");
    *count = cJSON_GetArraySize(arr);
    for (int i = 0; i < *count && i < 10; i++) {
        const char *v = cJSON_GetStringValue(cJSON_GetArrayItem(arr, i));
        if (v) snprintf(ids[i], 40, "%s", v);
    }
    cJSON_Delete(resp);
}

/* ========================================================================
 * Test: Full lifecycle — plan decomposition through goal completion
 *
 * Simulates building a small app:
 *   Goal: {backend_built, frontend_built, tests_passing}
 *   Phase 1 (compound): Set up infrastructure → backend_built
 *   Phase 2 (compound): Build frontend → frontend_built
 *   Phase 3 (primitive): Run tests → tests_passing
 *
 * The test walks through the entire supervisor lifecycle:
 *   1. Create goal
 *   2. Create initial compound + primitive actions
 *   3. Decompose first compound (children: setup DB, build API)
 *   4. Execute children sequentially, updating world state
 *   5. Mark compound as complete when children finish
 *   6. Continue to next phase
 *   7. Verify goal completion
 * ======================================================================== */

void test_full_lifecycle(void) {
    /* === Step 1: Create the goal === */
    ToolCall tc1 = make_tc("s1", "goap_create_goal",
        "{\"name\":\"Build small app\","
        "\"description\":\"Build a simple web app with backend, frontend, and tests\","
        "\"goal_state\":{\"backend_built\":true,\"frontend_built\":true,\"tests_passing\":true}}");
    ToolResult tr1 = {0};
    TEST_ASSERT_EQUAL(0, execute_goap_create_goal(&tc1, &tr1));
    TEST_ASSERT_EQUAL(1, tr1.success);

    char goal_id[40];
    extract_id(&tr1, "goal_id", goal_id, sizeof(goal_id));
    TEST_ASSERT_TRUE(strlen(goal_id) > 0);
    free_tc(&tc1);
    free_tr(&tr1);

    /* === Step 2: Create initial action plan (2 compound + 1 primitive) === */
    char args2[1024];
    snprintf(args2, sizeof(args2),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Set up backend infrastructure\","
        " \"is_compound\":true,\"preconditions\":[],"
        " \"effects\":[\"backend_built\"]},"
        "{\"description\":\"Build frontend application\","
        " \"is_compound\":true,\"preconditions\":[\"backend_built\"],"
        " \"effects\":[\"frontend_built\"]},"
        "{\"description\":\"Run full test suite\","
        " \"is_compound\":false,\"role\":\"testing\","
        " \"preconditions\":[\"backend_built\",\"frontend_built\"],"
        " \"effects\":[\"tests_passing\"]}"
        "]}", goal_id);

    ToolCall tc2 = make_tc("s2", "goap_create_actions", args2);
    ToolResult tr2 = {0};
    TEST_ASSERT_EQUAL(0, execute_goap_create_actions(&tc2, &tr2));
    TEST_ASSERT_EQUAL(1, tr2.success);

    char top_ids[10][40];
    int top_count = 0;
    extract_action_ids(&tr2, top_ids, &top_count);
    TEST_ASSERT_EQUAL(3, top_count);
    free_tc(&tc2);
    free_tr(&tr2);

    /* Phase 1 (compound), Phase 2 (compound), Phase 3 (primitive) */
    char *phase1_id = top_ids[0];
    char *phase2_id = top_ids[1];
    char *phase3_id = top_ids[2];

    /* === Step 3: Verify initial completion — should be 0/3 === */
    {
        char args[128];
        snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
        ToolCall tc = make_tc("s3", "goap_check_complete", args);
        ToolResult tr = {0};
        execute_goap_check_complete(&tc, &tr);

        cJSON *resp = parse_result(&tr);
        TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
        TEST_ASSERT_EQUAL(0, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "satisfied")));
        TEST_ASSERT_EQUAL(3, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "total")));
        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }

    /* === Step 4: List ready actions — Phase 1 should be ready (no preconditions) === */
    {
        char args[256];
        snprintf(args, sizeof(args),
            "{\"goal_id\":\"%s\",\"status\":\"pending\"}", goal_id);
        ToolCall tc = make_tc("s4", "goap_list_actions", args);
        ToolResult tr = {0};
        execute_goap_list_actions(&tc, &tr);

        cJSON *resp = parse_result(&tr);
        TEST_ASSERT_EQUAL(3, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "count")));
        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }

    /* === Step 5: Decompose Phase 1 — create 2 children === */
    {
        char args[1024];
        snprintf(args, sizeof(args),
            "{\"goal_id\":\"%s\",\"actions\":["
            "{\"description\":\"Create database schema\","
            " \"is_compound\":false,\"role\":\"implementation\","
            " \"preconditions\":[],\"effects\":[\"db_schema_exists\"],"
            " \"parent_action_id\":\"%s\"},"
            "{\"description\":\"Build REST API endpoints\","
            " \"is_compound\":false,\"role\":\"implementation\","
            " \"preconditions\":[\"db_schema_exists\"],\"effects\":[\"backend_built\"],"
            " \"parent_action_id\":\"%s\"}"
            "]}", goal_id, phase1_id, phase1_id);

        ToolCall tc = make_tc("s5", "goap_create_actions", args);
        ToolResult tr = {0};
        execute_goap_create_actions(&tc, &tr);
        TEST_ASSERT_EQUAL(1, tr.success);

        char child_ids[10][40];
        int child_count = 0;
        extract_action_ids(&tr, child_ids, &child_count);
        TEST_ASSERT_EQUAL(2, child_count);
        free_tc(&tc);
        free_tr(&tr);

        /* Mark Phase 1 compound as RUNNING */
        char uargs[256];
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"running\"}", phase1_id);
        ToolCall utc = make_tc("s5b", "goap_update_action", uargs);
        ToolResult utr = {0};
        execute_goap_update_action(&utc, &utr);
        TEST_ASSERT_EQUAL(1, utr.success);
        free_tc(&utc);
        free_tr(&utr);

        /* === Step 6: Execute child 1 (db schema) — no preconditions, should be ready === */

        /* Mark child 1 as running */
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"running\"}", child_ids[0]);
        ToolCall rc1 = make_tc("s6a", "goap_update_action", uargs);
        ToolResult rr1 = {0};
        execute_goap_update_action(&rc1, &rr1);
        TEST_ASSERT_EQUAL(1, rr1.success);
        free_tc(&rc1);
        free_tr(&rr1);

        /* Mark child 1 as completed with result */
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"completed\","
            "\"result\":\"Created users, posts, and follows tables in SQLite\"}",
            child_ids[0]);
        ToolCall cc1 = make_tc("s6b", "goap_update_action", uargs);
        ToolResult cr1 = {0};
        execute_goap_update_action(&cc1, &cr1);
        TEST_ASSERT_EQUAL(1, cr1.success);
        free_tc(&cc1);
        free_tr(&cr1);

        /* Verify effect: update world state for db_schema_exists */
        snprintf(uargs, sizeof(uargs),
            "{\"goal_id\":\"%s\",\"assertions\":{\"db_schema_exists\":true}}",
            goal_id);
        ToolCall ws1 = make_tc("s6c", "goap_update_world_state", uargs);
        ToolResult wr1 = {0};
        execute_goap_update_world_state(&ws1, &wr1);
        TEST_ASSERT_EQUAL(1, wr1.success);
        free_tc(&ws1);
        free_tr(&wr1);

        /* === Step 7: Execute child 2 (REST API) — precondition db_schema_exists now met === */

        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"running\"}", child_ids[1]);
        ToolCall rc2 = make_tc("s7a", "goap_update_action", uargs);
        ToolResult rr2 = {0};
        execute_goap_update_action(&rc2, &rr2);
        TEST_ASSERT_EQUAL(1, rr2.success);
        free_tc(&rc2);
        free_tr(&rr2);

        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"completed\","
            "\"result\":\"Built CRUD endpoints for users, posts, follows. JWT auth implemented.\"}",
            child_ids[1]);
        ToolCall cc2 = make_tc("s7b", "goap_update_action", uargs);
        ToolResult cr2 = {0};
        execute_goap_update_action(&cc2, &cr2);
        TEST_ASSERT_EQUAL(1, cr2.success);
        free_tc(&cc2);
        free_tr(&cr2);

        /* Verify effect: update world state for backend_built */
        snprintf(uargs, sizeof(uargs),
            "{\"goal_id\":\"%s\",\"assertions\":{\"backend_built\":true}}",
            goal_id);
        ToolCall ws2 = make_tc("s7c", "goap_update_world_state", uargs);
        ToolResult wr2 = {0};
        execute_goap_update_world_state(&ws2, &wr2);
        TEST_ASSERT_EQUAL(1, wr2.success);
        free_tc(&ws2);
        free_tr(&wr2);

        /* Mark Phase 1 compound as COMPLETED */
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"completed\"}", phase1_id);
        ToolCall pc1 = make_tc("s7d", "goap_update_action", uargs);
        ToolResult pr1 = {0};
        execute_goap_update_action(&pc1, &pr1);
        TEST_ASSERT_EQUAL(1, pr1.success);
        free_tc(&pc1);
        free_tr(&pr1);
    }

    /* === Step 8: Verify partial completion — 1/3 (backend_built) === */
    {
        char args[128];
        snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
        ToolCall tc = make_tc("s8", "goap_check_complete", args);
        ToolResult tr = {0};
        execute_goap_check_complete(&tc, &tr);

        cJSON *resp = parse_result(&tr);
        TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
        TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "satisfied")));

        cJSON *missing = cJSON_GetObjectItem(resp, "missing");
        TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(missing));
        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }

    /* === Step 9: Get completed action results — should have 2 children === */
    {
        char args[128];
        snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
        ToolCall tc = make_tc("s9", "goap_get_action_results", args);
        ToolResult tr = {0};
        execute_goap_get_action_results(&tc, &tr);

        cJSON *resp = parse_result(&tr);
        /* child 1 + child 2 = 2 with results (compound has no result text) */
        int count = (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "count"));
        TEST_ASSERT_EQUAL(2, count);
        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }

    /* === Step 10: Decompose + complete Phase 2 (frontend) === */
    {
        char args[1024];
        snprintf(args, sizeof(args),
            "{\"goal_id\":\"%s\",\"actions\":["
            "{\"description\":\"Build React UI with timeline and auth views\","
            " \"is_compound\":false,\"role\":\"implementation\","
            " \"preconditions\":[\"backend_built\"],\"effects\":[\"frontend_built\"],"
            " \"parent_action_id\":\"%s\"}"
            "]}", goal_id, phase2_id);

        ToolCall tc = make_tc("s10a", "goap_create_actions", args);
        ToolResult tr = {0};
        execute_goap_create_actions(&tc, &tr);
        TEST_ASSERT_EQUAL(1, tr.success);

        char fe_ids[10][40];
        int fe_count = 0;
        extract_action_ids(&tr, fe_ids, &fe_count);
        TEST_ASSERT_EQUAL(1, fe_count);
        free_tc(&tc);
        free_tr(&tr);

        /* Mark Phase 2 as RUNNING */
        char uargs[256];
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"running\"}", phase2_id);
        ToolCall utc = make_tc("s10b", "goap_update_action", uargs);
        ToolResult utr = {0};
        execute_goap_update_action(&utc, &utr);
        free_tc(&utc);
        free_tr(&utr);

        /* Execute frontend child */
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"running\"}", fe_ids[0]);
        ToolCall rc = make_tc("s10c", "goap_update_action", uargs);
        ToolResult rr = {0};
        execute_goap_update_action(&rc, &rr);
        free_tc(&rc);
        free_tr(&rr);

        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"completed\","
            "\"result\":\"React app with login, timeline, and follow components\"}", fe_ids[0]);
        ToolCall cc = make_tc("s10d", "goap_update_action", uargs);
        ToolResult cr = {0};
        execute_goap_update_action(&cc, &cr);
        free_tc(&cc);
        free_tr(&cr);

        /* Update world state */
        snprintf(uargs, sizeof(uargs),
            "{\"goal_id\":\"%s\",\"assertions\":{\"frontend_built\":true}}",
            goal_id);
        ToolCall ws = make_tc("s10e", "goap_update_world_state", uargs);
        ToolResult wr = {0};
        execute_goap_update_world_state(&ws, &wr);
        free_tc(&ws);
        free_tr(&wr);

        /* Mark Phase 2 as COMPLETED */
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"completed\"}", phase2_id);
        ToolCall pc = make_tc("s10f", "goap_update_action", uargs);
        ToolResult pr = {0};
        execute_goap_update_action(&pc, &pr);
        free_tc(&pc);
        free_tr(&pr);
    }

    /* === Step 11: Execute Phase 3 (testing — primitive, not compound) === */
    {
        /* Phase 3 preconditions: backend_built + frontend_built — both now true */
        char uargs[256];
        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"running\"}", phase3_id);
        ToolCall rc = make_tc("s11a", "goap_update_action", uargs);
        ToolResult rr = {0};
        execute_goap_update_action(&rc, &rr);
        free_tc(&rc);
        free_tr(&rr);

        snprintf(uargs, sizeof(uargs),
            "{\"action_id\":\"%s\",\"status\":\"completed\","
            "\"result\":\"All 42 tests passing. 100%% coverage on critical paths.\"}", phase3_id);
        ToolCall cc = make_tc("s11b", "goap_update_action", uargs);
        ToolResult cr = {0};
        execute_goap_update_action(&cc, &cr);
        free_tc(&cc);
        free_tr(&cr);

        snprintf(uargs, sizeof(uargs),
            "{\"goal_id\":\"%s\",\"assertions\":{\"tests_passing\":true}}",
            goal_id);
        ToolCall ws = make_tc("s11c", "goap_update_world_state", uargs);
        ToolResult wr = {0};
        execute_goap_update_world_state(&ws, &wr);
        free_tc(&ws);
        free_tr(&wr);
    }

    /* === Step 12: Verify goal completion — should be 3/3 === */
    {
        char args[128];
        snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
        ToolCall tc = make_tc("s12", "goap_check_complete", args);
        ToolResult tr = {0};
        execute_goap_check_complete(&tc, &tr);

        cJSON *resp = parse_result(&tr);
        TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
        TEST_ASSERT_EQUAL(3, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "satisfied")));
        TEST_ASSERT_NULL(cJSON_GetObjectItem(resp, "missing"));
        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }

    /* === Step 13: Verify final state via goal_status tool === */
    {
        char args[128];
        snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
        ToolCall tc = make_tc("s13", "goal_status", args);
        ToolResult tr = {0};
        execute_goal_status(&tc, &tr);
        TEST_ASSERT_EQUAL(1, tr.success);

        cJSON *resp = parse_result(&tr);
        TEST_ASSERT_EQUAL_STRING("Build small app",
            cJSON_GetStringValue(cJSON_GetObjectItem(resp, "name")));

        /* All world state assertions should be satisfied */
        TEST_ASSERT_EQUAL(3, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "assertions_satisfied")));
        TEST_ASSERT_EQUAL(3, (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "assertions_total")));

        /* Action tree should have 3 top-level actions */
        cJSON *tree = cJSON_GetObjectItem(resp, "action_tree");
        TEST_ASSERT_EQUAL(3, cJSON_GetArraySize(tree));

        /* Phase 1 should have 2 children */
        cJSON *p1 = cJSON_GetArrayItem(tree, 0);
        cJSON *children = cJSON_GetObjectItem(p1, "children");
        TEST_ASSERT_NOT_NULL(children);
        TEST_ASSERT_EQUAL(2, cJSON_GetArraySize(children));

        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }

    /* === Step 14: Verify all action results are retrievable === */
    {
        char args[128];
        snprintf(args, sizeof(args), "{\"goal_id\":\"%s\"}", goal_id);
        ToolCall tc = make_tc("s14", "goap_get_action_results", args);
        ToolResult tr = {0};
        execute_goap_get_action_results(&tc, &tr);

        cJSON *resp = parse_result(&tr);
        int count = (int)cJSON_GetNumberValue(
            cJSON_GetObjectItem(resp, "count"));
        /* 4 primitives with results: db schema, REST API, frontend, test suite */
        TEST_ASSERT_EQUAL(4, count);

        cJSON *results = cJSON_GetObjectItem(resp, "results");
        bool found_db = false, found_api = false, found_tests = false;
        cJSON *item;
        cJSON_ArrayForEach(item, results) {
            const char *r = cJSON_GetStringValue(
                cJSON_GetObjectItem(item, "result"));
            if (r && strstr(r, "SQLite")) found_db = true;
            if (r && strstr(r, "JWT")) found_api = true;
            if (r && strstr(r, "42 tests")) found_tests = true;
        }
        TEST_ASSERT_TRUE(found_db);
        TEST_ASSERT_TRUE(found_api);
        TEST_ASSERT_TRUE(found_tests);

        cJSON_Delete(resp);
        free_tc(&tc);
        free_tr(&tr);
    }
}

/* ========================================================================
 * Test: Readiness ordering — actions only become ready when preconditions met
 * ======================================================================== */

void test_readiness_ordering(void) {
    /* Create goal */
    ToolCall tc1 = make_tc("r1", "goap_create_goal",
        "{\"name\":\"Ordered build\","
        "\"description\":\"Test precondition ordering\","
        "\"goal_state\":{\"a\":true,\"b\":true,\"c\":true}}");
    ToolResult tr1 = {0};
    execute_goap_create_goal(&tc1, &tr1);
    char goal_id[40];
    extract_id(&tr1, "goal_id", goal_id, sizeof(goal_id));
    free_tc(&tc1);
    free_tr(&tr1);

    /* Create 3 actions with chained preconditions: A → B → C */
    char args[1024];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Step A\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"a\"]},"
        "{\"description\":\"Step B\",\"is_compound\":false,"
        " \"preconditions\":[\"a\"],\"effects\":[\"b\"]},"
        "{\"description\":\"Step C\",\"is_compound\":false,"
        " \"preconditions\":[\"a\",\"b\"],\"effects\":[\"c\"]}"
        "]}", goal_id);
    ToolCall tc2 = make_tc("r2", "goap_create_actions", args);
    ToolResult tr2 = {0};
    execute_goap_create_actions(&tc2, &tr2);

    char ids[10][40];
    int count = 0;
    extract_action_ids(&tr2, ids, &count);
    TEST_ASSERT_EQUAL(3, count);
    free_tc(&tc2);
    free_tr(&tr2);

    /* Initially: only Step A should be ready (empty world state) */
    size_t ready_count = 0;
    Action **ready = action_store_list_ready(as, goal_id, "{}", &ready_count);
    TEST_ASSERT_EQUAL(1, (int)ready_count);
    TEST_ASSERT_EQUAL_STRING("Step A", ready[0]->description);
    action_free_list(ready, ready_count);

    /* Complete A, update world state */
    action_store_update_status(as, ids[0], ACTION_STATUS_COMPLETED, "done");
    goal_store_update_world_state(gs, goal_id, "{\"a\":true}");

    /* Now Step B should be ready */
    ready = action_store_list_ready(as, goal_id, "{\"a\":true}", &ready_count);
    TEST_ASSERT_EQUAL(1, (int)ready_count);
    TEST_ASSERT_EQUAL_STRING("Step B", ready[0]->description);
    action_free_list(ready, ready_count);

    /* Complete B, update world state */
    action_store_update_status(as, ids[1], ACTION_STATUS_COMPLETED, "done");
    goal_store_update_world_state(gs, goal_id, "{\"a\":true,\"b\":true}");

    /* Now Step C should be ready */
    ready = action_store_list_ready(as, goal_id,
        "{\"a\":true,\"b\":true}", &ready_count);
    TEST_ASSERT_EQUAL(1, (int)ready_count);
    TEST_ASSERT_EQUAL_STRING("Step C", ready[0]->description);
    action_free_list(ready, ready_count);

    /* Complete C, verify goal complete */
    action_store_update_status(as, ids[2], ACTION_STATUS_COMPLETED, "done");
    goal_store_update_world_state(gs, goal_id,
        "{\"a\":true,\"b\":true,\"c\":true}");

    char cargs[128];
    snprintf(cargs, sizeof(cargs), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc3 = make_tc("r3", "goap_check_complete", cargs);
    ToolResult tr3 = {0};
    execute_goap_check_complete(&tc3, &tr3);

    cJSON *resp = parse_result(&tr3);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    cJSON_Delete(resp);
    free_tc(&tc3);
    free_tr(&tr3);
}

/* ========================================================================
 * Test: Parallel actions — independent actions are ready simultaneously
 * ======================================================================== */

void test_parallel_readiness(void) {
    ToolCall tc1 = make_tc("p1", "goap_create_goal",
        "{\"name\":\"Parallel work\","
        "\"description\":\"Test parallel readiness\","
        "\"goal_state\":{\"x\":true,\"y\":true,\"z\":true}}");
    ToolResult tr1 = {0};
    execute_goap_create_goal(&tc1, &tr1);
    char goal_id[40];
    extract_id(&tr1, "goal_id", goal_id, sizeof(goal_id));
    free_tc(&tc1);
    free_tr(&tr1);

    /* Create 3 independent actions (no preconditions) + 1 dependent */
    char args[1024];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Task X\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"x\"]},"
        "{\"description\":\"Task Y\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"y\"]},"
        "{\"description\":\"Task Z (depends on X and Y)\",\"is_compound\":false,"
        " \"preconditions\":[\"x\",\"y\"],\"effects\":[\"z\"]}"
        "]}", goal_id);
    ToolCall tc2 = make_tc("p2", "goap_create_actions", args);
    ToolResult tr2 = {0};
    execute_goap_create_actions(&tc2, &tr2);
    free_tc(&tc2);
    free_tr(&tr2);

    /* 2 actions should be ready simultaneously */
    size_t ready_count = 0;
    Action **ready = action_store_list_ready(as, goal_id, "{}", &ready_count);
    TEST_ASSERT_EQUAL(2, (int)ready_count);
    action_free_list(ready, ready_count);
}

/* ========================================================================
 * Test: World state merge — multiple updates accumulate
 * ======================================================================== */

void test_world_state_accumulation(void) {
    ToolCall tc1 = make_tc("w1", "goap_create_goal",
        "{\"name\":\"Merge test\","
        "\"description\":\"Test world state accumulation\","
        "\"goal_state\":{\"a\":true,\"b\":true,\"c\":true}}");
    ToolResult tr1 = {0};
    execute_goap_create_goal(&tc1, &tr1);
    char goal_id[40];
    extract_id(&tr1, "goal_id", goal_id, sizeof(goal_id));
    free_tc(&tc1);
    free_tr(&tr1);

    /* Update world state incrementally */
    char uargs[256];
    snprintf(uargs, sizeof(uargs),
        "{\"goal_id\":\"%s\",\"assertions\":{\"a\":true}}", goal_id);
    ToolCall tc2 = make_tc("w2", "goap_update_world_state", uargs);
    ToolResult tr2 = {0};
    execute_goap_update_world_state(&tc2, &tr2);
    free_tc(&tc2);
    free_tr(&tr2);

    /* Not complete yet */
    snprintf(uargs, sizeof(uargs), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc3 = make_tc("w3", "goap_check_complete", uargs);
    ToolResult tr3 = {0};
    execute_goap_check_complete(&tc3, &tr3);
    cJSON *resp = parse_result(&tr3);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    cJSON_Delete(resp);
    free_tc(&tc3);
    free_tr(&tr3);

    /* Add second assertion */
    snprintf(uargs, sizeof(uargs),
        "{\"goal_id\":\"%s\",\"assertions\":{\"b\":true}}", goal_id);
    ToolCall tc4 = make_tc("w4", "goap_update_world_state", uargs);
    ToolResult tr4 = {0};
    execute_goap_update_world_state(&tc4, &tr4);
    free_tc(&tc4);
    free_tr(&tr4);

    /* Still not complete */
    snprintf(uargs, sizeof(uargs), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc5 = make_tc("w5", "goap_check_complete", uargs);
    ToolResult tr5 = {0};
    execute_goap_check_complete(&tc5, &tr5);
    resp = parse_result(&tr5);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp, "satisfied")));
    cJSON_Delete(resp);
    free_tc(&tc5);
    free_tr(&tr5);

    /* Add third assertion — now complete */
    snprintf(uargs, sizeof(uargs),
        "{\"goal_id\":\"%s\",\"assertions\":{\"c\":true}}", goal_id);
    ToolCall tc6 = make_tc("w6", "goap_update_world_state", uargs);
    ToolResult tr6 = {0};
    execute_goap_update_world_state(&tc6, &tr6);
    free_tc(&tc6);
    free_tr(&tr6);

    snprintf(uargs, sizeof(uargs), "{\"goal_id\":\"%s\"}", goal_id);
    ToolCall tc7 = make_tc("w7", "goap_check_complete", uargs);
    ToolResult tr7 = {0};
    execute_goap_check_complete(&tc7, &tr7);
    resp = parse_result(&tr7);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp, "complete")));
    cJSON_Delete(resp);
    free_tc(&tc7);
    free_tr(&tr7);
}

/* ========================================================================
 * Test: Multi-goal isolation — goals don't share world state or actions
 * ======================================================================== */

void test_multi_goal_isolation(void) {
    /* Create two independent goals */
    ToolCall tc1 = make_tc("m1", "goap_create_goal",
        "{\"name\":\"Goal Alpha\","
        "\"description\":\"First goal\","
        "\"goal_state\":{\"alpha_done\":true}}");
    ToolResult tr1 = {0};
    execute_goap_create_goal(&tc1, &tr1);
    char goal_a[40];
    extract_id(&tr1, "goal_id", goal_a, sizeof(goal_a));
    free_tc(&tc1);
    free_tr(&tr1);

    ToolCall tc2 = make_tc("m2", "goap_create_goal",
        "{\"name\":\"Goal Beta\","
        "\"description\":\"Second goal\","
        "\"goal_state\":{\"beta_done\":true}}");
    ToolResult tr2 = {0};
    execute_goap_create_goal(&tc2, &tr2);
    char goal_b[40];
    extract_id(&tr2, "goal_id", goal_b, sizeof(goal_b));
    free_tc(&tc2);
    free_tr(&tr2);

    /* Create actions for each */
    char args_a[512];
    snprintf(args_a, sizeof(args_a),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Alpha work\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"alpha_done\"]}"
        "]}", goal_a);
    ToolCall tc3 = make_tc("m3", "goap_create_actions", args_a);
    ToolResult tr3 = {0};
    execute_goap_create_actions(&tc3, &tr3);
    free_tc(&tc3);
    free_tr(&tr3);

    char args_b[512];
    snprintf(args_b, sizeof(args_b),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Beta work\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"beta_done\"]}"
        "]}", goal_b);
    ToolCall tc4 = make_tc("m4", "goap_create_actions", args_b);
    ToolResult tr4 = {0};
    execute_goap_create_actions(&tc4, &tr4);
    free_tc(&tc4);
    free_tr(&tr4);

    /* Complete Goal Alpha's world state */
    char uargs[256];
    snprintf(uargs, sizeof(uargs),
        "{\"goal_id\":\"%s\",\"assertions\":{\"alpha_done\":true}}", goal_a);
    ToolCall tc5 = make_tc("m5", "goap_update_world_state", uargs);
    ToolResult tr5 = {0};
    execute_goap_update_world_state(&tc5, &tr5);
    free_tc(&tc5);
    free_tr(&tr5);

    /* Goal Alpha should be complete */
    snprintf(uargs, sizeof(uargs), "{\"goal_id\":\"%s\"}", goal_a);
    ToolCall tc6 = make_tc("m6", "goap_check_complete", uargs);
    ToolResult tr6 = {0};
    execute_goap_check_complete(&tc6, &tr6);
    cJSON *resp_a = parse_result(&tr6);
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(resp_a, "complete")));
    cJSON_Delete(resp_a);
    free_tc(&tc6);
    free_tr(&tr6);

    /* Goal Beta should NOT be complete — isolated world state */
    snprintf(uargs, sizeof(uargs), "{\"goal_id\":\"%s\"}", goal_b);
    ToolCall tc7 = make_tc("m7", "goap_check_complete", uargs);
    ToolResult tr7 = {0};
    execute_goap_check_complete(&tc7, &tr7);
    cJSON *resp_b = parse_result(&tr7);
    TEST_ASSERT_FALSE(cJSON_IsTrue(cJSON_GetObjectItem(resp_b, "complete")));
    cJSON_Delete(resp_b);
    free_tc(&tc7);
    free_tr(&tr7);

    /* Actions for Goal A should not appear in Goal B's listing */
    char largs[128];
    snprintf(largs, sizeof(largs), "{\"goal_id\":\"%s\"}", goal_b);
    ToolCall tc8 = make_tc("m8", "goap_list_actions", largs);
    ToolResult tr8 = {0};
    execute_goap_list_actions(&tc8, &tr8);
    cJSON *resp_l = parse_result(&tr8);
    TEST_ASSERT_EQUAL(1, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp_l, "count")));
    cJSON *actions = cJSON_GetObjectItem(resp_l, "actions");
    cJSON *first = cJSON_GetArrayItem(actions, 0);
    TEST_ASSERT_EQUAL_STRING("Beta work",
        cJSON_GetStringValue(cJSON_GetObjectItem(first, "description")));
    cJSON_Delete(resp_l);
    free_tc(&tc8);
    free_tr(&tr8);

    /* list_goals should show both */
    ToolCall tc9 = make_tc("m9", "list_goals", "{}");
    ToolResult tr9 = {0};
    execute_list_goals(&tc9, &tr9);
    cJSON *resp_g = parse_result(&tr9);
    TEST_ASSERT_EQUAL(2, (int)cJSON_GetNumberValue(
        cJSON_GetObjectItem(resp_g, "count")));
    cJSON_Delete(resp_g);
    free_tc(&tc9);
    free_tr(&tr9);
}

/* ========================================================================
 * Test: Replanning — skip pending actions and create replacements
 * ======================================================================== */

void test_replan_skip_pending(void) {
    ToolCall tc1 = make_tc("k1", "goap_create_goal",
        "{\"name\":\"Replan test\","
        "\"description\":\"Test replanning\","
        "\"goal_state\":{\"done\":true}}");
    ToolResult tr1 = {0};
    execute_goap_create_goal(&tc1, &tr1);
    char goal_id[40];
    extract_id(&tr1, "goal_id", goal_id, sizeof(goal_id));
    free_tc(&tc1);
    free_tr(&tr1);

    /* Create initial actions */
    char args[512];
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Original approach\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"done\"]}"
        "]}", goal_id);
    ToolCall tc2 = make_tc("k2", "goap_create_actions", args);
    ToolResult tr2 = {0};
    execute_goap_create_actions(&tc2, &tr2);
    free_tc(&tc2);
    free_tr(&tr2);

    /* Skip all pending actions (replanning) */
    action_store_skip_pending(as, goal_id);

    /* Verify skipped actions don't show as ready */
    size_t ready_count = 0;
    Action **ready = action_store_list_ready(as, goal_id, "{}", &ready_count);
    TEST_ASSERT_EQUAL(0, (int)ready_count);
    action_free_list(ready, ready_count);

    /* Create replacement action */
    snprintf(args, sizeof(args),
        "{\"goal_id\":\"%s\",\"actions\":["
        "{\"description\":\"Better approach\",\"is_compound\":false,"
        " \"preconditions\":[],\"effects\":[\"done\"]}"
        "]}", goal_id);
    ToolCall tc3 = make_tc("k3", "goap_create_actions", args);
    ToolResult tr3 = {0};
    execute_goap_create_actions(&tc3, &tr3);
    TEST_ASSERT_EQUAL(1, tr3.success);
    free_tc(&tc3);
    free_tr(&tr3);

    /* New action should be ready */
    ready = action_store_list_ready(as, goal_id, "{}", &ready_count);
    TEST_ASSERT_EQUAL(1, (int)ready_count);
    TEST_ASSERT_EQUAL_STRING("Better approach", ready[0]->description);
    action_free_list(ready, ready_count);
}

/* ========================================================================
 * Runner
 * ======================================================================== */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_full_lifecycle);
    RUN_TEST(test_readiness_ordering);
    RUN_TEST(test_parallel_readiness);
    RUN_TEST(test_world_state_accumulation);
    RUN_TEST(test_multi_goal_isolation);
    RUN_TEST(test_replan_skip_pending);

    return UNITY_END();
}
