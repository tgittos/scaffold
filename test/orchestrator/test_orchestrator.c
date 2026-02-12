#include "unity.h"
#include "orchestrator/orchestrator.h"
#include "db/goal_store.h"
#include "util/app_home.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *TEST_DB_PATH = "/tmp/test_orchestrator.db";
static goal_store_t *g_store = NULL;

void setUp(void) {
    app_home_init(NULL);
    unlink(TEST_DB_PATH);
    g_store = goal_store_create(TEST_DB_PATH);
}

void tearDown(void) {
    if (g_store != NULL) {
        goal_store_destroy(g_store);
        g_store = NULL;
    }
    unlink(TEST_DB_PATH);
    app_home_cleanup();
}

static void create_test_goal(const char *name, char *out_id) {
    goal_store_insert(g_store, name, "test goal",
                      "{\"done\": true}", "test-queue", out_id);
}

static pid_t fork_sleeper(void) {
    pid_t pid = fork();
    if (pid == 0) {
        pause();
        _exit(0);
    }
    return pid;
}

static int64_t now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void test_supervisor_alive_no_supervisor(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(g_store, goal_id));
}

void test_supervisor_alive_with_running_process(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());

    TEST_ASSERT_TRUE(orchestrator_supervisor_alive(g_store, goal_id));

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_supervisor_alive_with_dead_process(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(g_store, goal_id));

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    goal_free(goal);
}

void test_kill_supervisor(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());
    goal_store_update_status(g_store, goal_id, GOAL_STATUS_ACTIVE);

    int rc = orchestrator_kill_supervisor(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, rc);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    TEST_ASSERT_EQUAL(GOAL_STATUS_PAUSED, goal->status);
    goal_free(goal);

    TEST_ASSERT_EQUAL(-1, kill(pid, 0));
}

void test_kill_supervisor_no_supervisor(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    TEST_ASSERT_EQUAL(-1, orchestrator_kill_supervisor(g_store, goal_id));
}

void test_reap_supervisors(void) {
    char id1[40], id2[40];
    create_test_goal("goal1", id1);
    create_test_goal("goal2", id2);

    pid_t pid1 = fork_sleeper();
    pid_t pid2 = fork_sleeper();
    TEST_ASSERT_TRUE(pid1 > 0);
    TEST_ASSERT_TRUE(pid2 > 0);

    goal_store_update_status(g_store, id1, GOAL_STATUS_ACTIVE);
    goal_store_update_status(g_store, id2, GOAL_STATUS_ACTIVE);
    goal_store_update_supervisor(g_store, id1, pid1, now_millis());
    goal_store_update_supervisor(g_store, id2, pid2, now_millis());

    kill(pid1, SIGKILL);
    waitpid(pid1, NULL, 0);

    orchestrator_reap_supervisors(g_store);

    Goal *goal1 = goal_store_get(g_store, id1);
    TEST_ASSERT_EQUAL(0, goal1->supervisor_pid);
    goal_free(goal1);

    Goal *goal2 = goal_store_get(g_store, id2);
    TEST_ASSERT_EQUAL(pid2, goal2->supervisor_pid);
    goal_free(goal2);

    kill(pid2, SIGKILL);
    waitpid(pid2, NULL, 0);
}

void test_reap_non_active_goal(void) {
    char goal_id[40];
    create_test_goal("paused", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    goal_store_update_status(g_store, goal_id, GOAL_STATUS_PAUSED);
    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    orchestrator_reap_supervisors(g_store);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    goal_free(goal);
}

void test_check_stale_dead_pid(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    orchestrator_check_stale(g_store);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    goal_free(goal);
}

void test_check_stale_recent_running_not_cleared(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());

    orchestrator_check_stale(g_store);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(pid, goal->supervisor_pid);
    goal_free(goal);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_check_stale_old_running_cleared(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    int64_t two_hours_ago = now_millis() - (2 * 3600 * 1000);
    goal_store_update_supervisor(g_store, goal_id, pid, two_hours_ago);

    orchestrator_check_stale(g_store);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    goal_free(goal);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_respawn_dead_no_active_goals(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    int count = orchestrator_respawn_dead(g_store);
    TEST_ASSERT_EQUAL(0, count);
}

void test_null_params(void) {
    TEST_ASSERT_EQUAL(-1, orchestrator_spawn_supervisor(NULL, "id"));
    TEST_ASSERT_EQUAL(-1, orchestrator_spawn_supervisor(g_store, NULL));
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(NULL, "id"));
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(g_store, NULL));
    orchestrator_reap_supervisors(NULL);
    TEST_ASSERT_EQUAL(-1, orchestrator_kill_supervisor(NULL, "id"));
    TEST_ASSERT_EQUAL(-1, orchestrator_kill_supervisor(g_store, NULL));
    orchestrator_check_stale(NULL);
    TEST_ASSERT_EQUAL(-1, orchestrator_respawn_dead(NULL));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_supervisor_alive_no_supervisor);
    RUN_TEST(test_supervisor_alive_with_running_process);
    RUN_TEST(test_supervisor_alive_with_dead_process);
    RUN_TEST(test_kill_supervisor);
    RUN_TEST(test_kill_supervisor_no_supervisor);
    RUN_TEST(test_reap_supervisors);
    RUN_TEST(test_reap_non_active_goal);
    RUN_TEST(test_check_stale_dead_pid);
    RUN_TEST(test_check_stale_recent_running_not_cleared);
    RUN_TEST(test_check_stale_old_running_cleared);
    RUN_TEST(test_respawn_dead_no_active_goals);
    RUN_TEST(test_null_params);

    return UNITY_END();
}
