#include "unity.h"
#include "orchestrator/orchestrator.h"
#include "db/goal_store.h"
#include "util/app_home.h"
#include "util/process_spawn.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../test_fs_utils.h"

static const char *TEST_DB_PATH = "/tmp/test_orchestrator.db";
static goal_store_t *g_store = NULL;

void setUp(void) {
    app_home_init(NULL);
    unlink_sqlite_db(TEST_DB_PATH);
    g_store = goal_store_create(TEST_DB_PATH);
}

void tearDown(void) {
    if (g_store != NULL) {
        goal_store_destroy(g_store);
        g_store = NULL;
    }
    unlink_sqlite_db(TEST_DB_PATH);
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

/*
 * Fork a sleeper that holds the write end of a pipe. Reading that pipe to EOF
 * proves the child has exited without reaping it, so the caller can observe it
 * as a zombie instead of racing asynchronous signal delivery.
 */
static pid_t fork_sleeper_watched(int *out_read_fd) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        pause();
        _exit(0);
    }

    close(pipefd[1]);
    *out_read_fd = pipefd[0];
    return pid;
}

/* Block until the watched child's fds close, i.e. until it has exited. */
static void await_exit(int read_fd) {
    char byte;
    while (read(read_fd, &byte, 1) > 0) { }
    close(read_fd);
}

static int64_t now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Minimal in-memory goal for the liveness predicate. */
static Goal make_goal(pid_t pid, time_t heartbeat_at) {
    Goal g;
    memset(&g, 0, sizeof(g));
    g.supervisor_pid = pid;
    g.supervisor_heartbeat_at = heartbeat_at;
    return g;
}

void test_supervisor_alive_no_supervisor(void) {
    time_t now = time(NULL);
    Goal g = make_goal(0, 0);
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(&g, now));
}

void test_supervisor_alive_with_running_process(void) {
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    time_t now = time(NULL);
    Goal g = make_goal(pid, now);
    TEST_ASSERT_TRUE(orchestrator_supervisor_alive(&g, now));

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_supervisor_alive_with_dead_process(void) {
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    time_t now = time(NULL);
    Goal g = make_goal(pid, now);
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(&g, now));
}

void test_alive_requires_fresh_heartbeat(void) {
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    time_t now = time(NULL);
    Goal stale = make_goal(pid, now - 60);
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(&stale, now));

    Goal fresh = make_goal(pid, now - 5);
    TEST_ASSERT_TRUE(orchestrator_supervisor_alive(&fresh, now));

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_zombie_not_alive(void) {
    int read_fd = -1;
    pid_t pid = fork_sleeper_watched(&read_fd);
    TEST_ASSERT_TRUE(pid > 0);

    /* Kill without reaping: the child becomes a zombie, which still answers
     * kill(pid, 0). */
    kill(pid, SIGKILL);
    await_exit(read_fd);

    time_t now = time(NULL);
    Goal g = make_goal(pid, now);
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(&g, now));

    waitpid(pid, NULL, WNOHANG);
}

void test_supervisor_alive_null_goal(void) {
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(NULL, time(NULL)));
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

void test_check_stale_old_running_not_cleared(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);

    int64_t two_hours_ago = now_millis() - (2 * 3600 * 1000);
    goal_store_update_supervisor(g_store, goal_id, pid, two_hours_ago);

    orchestrator_check_stale(g_store);

    /* Live processes are never cleared regardless of age */
    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(pid, goal->supervisor_pid);
    goal_free(goal);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_respawn_dead_no_respawnable_goals(void) {
    char goal_id[40];
    create_test_goal("test", goal_id);
    /* Set to PAUSED — only PLANNING and ACTIVE goals are respawned */
    goal_store_update_status(g_store, goal_id, GOAL_STATUS_PAUSED);
    int count = orchestrator_respawn_dead(g_store, goal_project_key());
    TEST_ASSERT_EQUAL(0, count);
}

void test_respawn_dead_planning_goal(void) {
    char goal_id[40];
    create_test_goal("planning goal", goal_id);
    /* Goals start in PLANNING status — should be respawned when pid=0 */
    TEST_ASSERT_EQUAL(GOAL_STATUS_PLANNING, GOAL_STATUS_PLANNING);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    TEST_ASSERT_EQUAL(GOAL_STATUS_PLANNING, goal->status);
    TEST_ASSERT_EQUAL(0, goal->supervisor_pid);
    goal_free(goal);

    int count = orchestrator_respawn_dead(g_store, goal_project_key());
    TEST_ASSERT_EQUAL(1, count);

    /* Verify a supervisor was assigned */
    goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    TEST_ASSERT_TRUE(goal->supervisor_pid > 0);

    /* Clean up spawned process */
    if (goal->supervisor_pid > 0) {
        kill(goal->supervisor_pid, SIGKILL);
        waitpid(goal->supervisor_pid, NULL, 0);
    }
    goal_free(goal);
}

void test_respawn_dead_both_planning_and_active(void) {
    char id1[40], id2[40];
    create_test_goal("planning", id1);
    create_test_goal("active", id2);
    goal_store_update_status(g_store, id2, GOAL_STATUS_ACTIVE);

    int count = orchestrator_respawn_dead(g_store, goal_project_key());
    TEST_ASSERT_EQUAL(2, count);

    /* Clean up */
    Goal *g1 = goal_store_get(g_store, id1);
    Goal *g2 = goal_store_get(g_store, id2);
    if (g1 && g1->supervisor_pid > 0) {
        kill(g1->supervisor_pid, SIGKILL);
        waitpid(g1->supervisor_pid, NULL, 0);
    }
    if (g2 && g2->supervisor_pid > 0) {
        kill(g2->supervisor_pid, SIGKILL);
        waitpid(g2->supervisor_pid, NULL, 0);
    }
    goal_free(g1);
    goal_free(g2);
}

void test_killed_supervisor_respawned(void) {
    char goal_id[40];
    create_test_goal("killed supervisor", goal_id);
    goal_store_update_status(g_store, goal_id, GOAL_STATUS_ACTIVE);

    int read_fd = -1;
    pid_t pid = fork_sleeper_watched(&read_fd);
    TEST_ASSERT_TRUE(pid > 0);
    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());

    /* Kill -9 without reaping: the pid lingers as a zombie in the store. */
    kill(pid, SIGKILL);
    await_exit(read_fd);

    int count = orchestrator_respawn_dead(g_store, goal_project_key());
    TEST_ASSERT_EQUAL(1, count);

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    TEST_ASSERT_TRUE(goal->supervisor_pid > 0);
    TEST_ASSERT_NOT_EQUAL(pid, goal->supervisor_pid);

    kill(goal->supervisor_pid, SIGKILL);
    waitpid(goal->supervisor_pid, NULL, 0);
    goal_free(goal);
    waitpid(pid, NULL, WNOHANG);
}

void test_live_supervisor_not_respawned(void) {
    char goal_id[40];
    create_test_goal("live supervisor", goal_id);
    goal_store_update_status(g_store, goal_id, GOAL_STATUS_ACTIVE);

    pid_t pid = fork_sleeper();
    TEST_ASSERT_TRUE(pid > 0);
    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());
    goal_store_heartbeat(g_store, goal_id, time(NULL));

    TEST_ASSERT_EQUAL(0, orchestrator_respawn_dead(g_store, goal_project_key()));

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_EQUAL(pid, goal->supervisor_pid);
    goal_free(goal);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void test_respawn_dead_skips_other_project(void) {
    char goal_id[40];
    create_test_goal("other project goal", goal_id);
    goal_store_update_status(g_store, goal_id, GOAL_STATUS_ACTIVE);

    int read_fd = -1;
    pid_t pid = fork_sleeper_watched(&read_fd);
    TEST_ASSERT_TRUE(pid > 0);
    goal_store_update_supervisor(g_store, goal_id, pid, now_millis());
    kill(pid, SIGKILL);
    await_exit(read_fd);

    /* The goal belongs to this project; another project must not touch it. */
    TEST_ASSERT_EQUAL(0, orchestrator_respawn_dead(g_store, "goals_elsewhere_dead"));

    Goal *goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    TEST_ASSERT_EQUAL(pid, goal->supervisor_pid);
    goal_free(goal);

    /* Its own project still respawns it. */
    TEST_ASSERT_EQUAL(1, orchestrator_respawn_dead(g_store, goal_project_key()));

    goal = goal_store_get(g_store, goal_id);
    TEST_ASSERT_NOT_NULL(goal);
    if (goal->supervisor_pid > 0) {
        kill(goal->supervisor_pid, SIGKILL);
        waitpid(goal->supervisor_pid, NULL, 0);
    }
    goal_free(goal);
    waitpid(pid, NULL, WNOHANG);
}

void test_spawn_logged_writes_file(void) {
    const char *log_path = "/tmp/test_orchestrator_spawn.log";
    unlink(log_path);

    char *argv[] = {
        "/bin/sh", "-c",
        "echo out-marker; echo err-marker >&2",
        NULL
    };

    pid_t pid = process_spawn_logged(argv, log_path);
    TEST_ASSERT_TRUE(pid > 0);

    int status = 0;
    TEST_ASSERT_EQUAL(pid, waitpid(pid, &status, 0));

    char buf[256] = {0};
    FILE *f = fopen(log_path, "r");
    TEST_ASSERT_NOT_NULL(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    TEST_ASSERT_NOT_NULL(strstr(buf, "out-marker"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "err-marker"));

    unlink(log_path);
}

void test_spawn_logged_null_params(void) {
    char *argv[] = { "/bin/sh", NULL };
    char *empty[] = { NULL };
    TEST_ASSERT_EQUAL(-1, process_spawn_logged(NULL, "/tmp/x.log"));
    TEST_ASSERT_EQUAL(-1, process_spawn_logged(empty, "/tmp/x.log"));
    TEST_ASSERT_EQUAL(-1, process_spawn_logged(argv, NULL));
}

void test_supervisor_log_path(void) {
    char *path = orchestrator_supervisor_log_path("abc123");
    TEST_ASSERT_NOT_NULL(path);
    TEST_ASSERT_NOT_NULL(strstr(path, "logs/supervisor-abc123.log"));
    free(path);

    TEST_ASSERT_NULL(orchestrator_supervisor_log_path(NULL));
}

void test_null_params(void) {
    TEST_ASSERT_EQUAL(-1, orchestrator_spawn_supervisor(NULL, "id"));
    TEST_ASSERT_EQUAL(-1, orchestrator_spawn_supervisor(g_store, NULL));
    TEST_ASSERT_FALSE(orchestrator_supervisor_alive(NULL, time(NULL)));
    orchestrator_reap_supervisors(NULL);
    TEST_ASSERT_EQUAL(-1, orchestrator_kill_supervisor(NULL, "id"));
    TEST_ASSERT_EQUAL(-1, orchestrator_kill_supervisor(g_store, NULL));
    orchestrator_check_stale(NULL);
    TEST_ASSERT_EQUAL(-1, orchestrator_respawn_dead(NULL, goal_project_key()));
    TEST_ASSERT_EQUAL(-1, orchestrator_respawn_dead(g_store, NULL));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_supervisor_alive_no_supervisor);
    RUN_TEST(test_supervisor_alive_with_running_process);
    RUN_TEST(test_supervisor_alive_with_dead_process);
    RUN_TEST(test_alive_requires_fresh_heartbeat);
    RUN_TEST(test_zombie_not_alive);
    RUN_TEST(test_supervisor_alive_null_goal);
    RUN_TEST(test_kill_supervisor);
    RUN_TEST(test_kill_supervisor_no_supervisor);
    RUN_TEST(test_reap_supervisors);
    RUN_TEST(test_reap_non_active_goal);
    RUN_TEST(test_check_stale_dead_pid);
    RUN_TEST(test_check_stale_recent_running_not_cleared);
    RUN_TEST(test_check_stale_old_running_not_cleared);
    RUN_TEST(test_respawn_dead_no_respawnable_goals);
    RUN_TEST(test_respawn_dead_planning_goal);
    RUN_TEST(test_respawn_dead_both_planning_and_active);
    RUN_TEST(test_killed_supervisor_respawned);
    RUN_TEST(test_live_supervisor_not_respawned);
    RUN_TEST(test_respawn_dead_skips_other_project);
    RUN_TEST(test_spawn_logged_writes_file);
    RUN_TEST(test_spawn_logged_null_params);
    RUN_TEST(test_supervisor_log_path);
    RUN_TEST(test_null_params);

    return UNITY_END();
}
