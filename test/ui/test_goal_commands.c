#include "../../test/unity/unity.h"
#include "ui/goal_commands.h"
#include "agent/session.h"
#include "services/services.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "util/app_home.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "../test_fs_utils.h"

static AgentSession g_session;
static Services *g_services;
static const char *TEST_DB = "/tmp/test_goal_commands.db";

static const char *CAPTURE_PATH = "/tmp/test_goal_commands_capture.txt";
static char g_capture[16384];
static int g_real_stdout = -1;

/* Point stdout at a file, keeping the test runner's own stdout for Unity. */
static void stdout_to(const char *path) {
    fflush(stdout);
    if (g_real_stdout < 0) g_real_stdout = dup(STDOUT_FILENO);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
}

static void redirect_stdout(void) {
    stdout_to("/dev/null");
}

static void restore_stdout(void) {
    fflush(stdout);
    if (g_real_stdout >= 0) dup2(g_real_stdout, STDOUT_FILENO);
}

/* Run a /goals command with stdout captured into g_capture. */
static int run_capturing(const char *args, AgentSession *session) {
    stdout_to(CAPTURE_PATH);
    int rc = process_goals_command(args, session);
    redirect_stdout();

    g_capture[0] = '\0';
    FILE *f = fopen(CAPTURE_PATH, "r");
    if (f != NULL) {
        size_t n = fread(g_capture, 1, sizeof(g_capture) - 1, f);
        g_capture[n] = '\0';
        fclose(f);
    }
    unlink(CAPTURE_PATH);
    return rc;
}

void setUp(void) {
    app_home_init(NULL);
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
    app_home_cleanup();
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

void test_goals_shows_stalled(void) {
    char id[40];
    goal_store_insert(g_services->goal_store, "Stalled Goal",
                      "An active goal with no live supervisor",
                      "{\"done\": true}", "stalled_queue", id);
    goal_store_update_status(g_services->goal_store, id, GOAL_STATUS_ACTIVE);

    int rc = run_capturing("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(strstr(g_capture, "stalled"));
}

void test_goals_shows_action_counts(void) {
    char id[40];
    goal_store_insert(g_services->goal_store, "Counted Goal", "Goal with actions",
                      "{\"a\":true,\"b\":true,\"c\":true,\"d\":true,"
                      "\"e\":true,\"f\":true,\"g\":true}",
                      "count_queue", id);
    goal_store_update_world_state(g_services->goal_store, id,
                                  "{\"a\":true,\"b\":true,\"c\":false}");

    char action_ids[14][40];
    for (int i = 0; i < 14; i++) {
        char desc[32];
        snprintf(desc, sizeof(desc), "action %d", i);
        action_store_insert(g_services->action_store, id, NULL, desc,
                            "[]", "[]", false, "implementation", action_ids[i]);
    }
    for (int i = 0; i < 3; i++) {
        action_store_update_status(g_services->action_store, action_ids[i],
                                   ACTION_STATUS_COMPLETED, "done");
    }

    int rc = run_capturing("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(strstr(g_capture, "actions 3/14"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture, "state 2/7"));
}

void test_goals_detail_shows_log_path(void) {
    char id[40];
    goal_store_insert(g_services->goal_store, "Logged Goal", "Goal with a log",
                      "{\"done\": true}", "log_queue", id);

    char expected[128];
    snprintf(expected, sizeof(expected), "logs/supervisor-%s.log", id);

    int rc = run_capturing(id, &g_session);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_NOT_NULL(strstr(g_capture, expected));
}

/* Create a minimal git repo so project scoping resolves to this directory. */
static void make_git_repo(const char *path) {
    mkdir(path, 0755);
    char git_dir[PATH_MAX];
    int n = snprintf(git_dir, sizeof(git_dir) - 1, "%s", path);
    if (n > 0 && n < (int)(sizeof(git_dir) - 6)) {
        memcpy(git_dir + n, "/.git", 6);
        mkdir(git_dir, 0755);
    }
}

void test_goals_list_scoped_to_project(void) {
    char saved_cwd[PATH_MAX];
    TEST_ASSERT_NOT_NULL(getcwd(saved_cwd, sizeof(saved_cwd)));

    char base[] = "/tmp/test_goal_cmd_scope_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(base));

    char proj_a[PATH_MAX], proj_b[PATH_MAX];
    int base_len = (int)strlen(base);
    memcpy(proj_a, base, (size_t)base_len);
    memcpy(proj_a + base_len, "/projA", 7);
    memcpy(proj_b, base, (size_t)base_len);
    memcpy(proj_b + base_len, "/projB", 7);
    make_git_repo(proj_a);
    make_git_repo(proj_b);

    /* Create the goal while project A is the working directory. */
    TEST_ASSERT_EQUAL_INT(0, chdir(proj_a));
    char id[40];
    TEST_ASSERT_EQUAL_INT(0, goal_store_insert(g_services->goal_store, "ProjAGoal",
                                               "only project A", "{\"done\": true}",
                                               "a_queue", id));

    TEST_ASSERT_EQUAL_INT(0, run_capturing("", &g_session));
    TEST_ASSERT_NOT_NULL(strstr(g_capture, "ProjAGoal"));

    /* Project B must not see it. */
    TEST_ASSERT_EQUAL_INT(0, chdir(proj_b));
    TEST_ASSERT_EQUAL_INT(0, run_capturing("", &g_session));
    TEST_ASSERT_NULL(strstr(g_capture, "ProjAGoal"));
    TEST_ASSERT_NOT_NULL(strstr(g_capture, "No goals in this project"));

    TEST_ASSERT_EQUAL_INT(0, chdir(saved_cwd));
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
    RUN_TEST(test_goals_shows_stalled);
    RUN_TEST(test_goals_shows_action_counts);
    RUN_TEST(test_goals_detail_shows_log_path);
    RUN_TEST(test_goals_list_scoped_to_project);
    RUN_TEST(test_goals_no_store);

    return UNITY_END();
}
