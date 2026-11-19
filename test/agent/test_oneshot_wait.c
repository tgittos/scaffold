#include "unity.h"
#include "agent/oneshot_wait.h"
#include "orchestrator/orchestrator.h"
#include "db/goal_store.h"
#include "services/services.h"
#include "util/app_home.h"
#include "util/interrupt.h"
#include "../test_fs_utils.h"
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char g_goal_db_path[256];
static char g_msg_db_path[256];
static goal_store_t *g_gs = NULL;
static message_store_t *g_ms = NULL;
static Services *g_svc = NULL;
static AgentSession g_session;

void setUp(void) {
    app_home_init(NULL);
    snprintf(g_goal_db_path, sizeof(g_goal_db_path), "/tmp/test_oneshot_wait_goals_%d.db", getpid());
    snprintf(g_msg_db_path, sizeof(g_msg_db_path), "/tmp/test_oneshot_wait_msgs_%d.db", getpid());
    unlink_sqlite_db(g_goal_db_path);
    unlink_sqlite_db(g_msg_db_path);

    g_gs = goal_store_create(g_goal_db_path);
    g_ms = message_store_create(g_msg_db_path);
    g_svc = services_create_empty();
    g_svc->goal_store = g_gs;
    g_svc->message_store = g_ms;

    memset(&g_session, 0, sizeof(g_session));
    g_session.services = g_svc;

    interrupt_cleanup();
}

void tearDown(void) {
    interrupt_cleanup();
    if (g_svc != NULL) {
        /* services_destroy() frees goal_store/message_store too. */
        services_destroy(g_svc);
        g_svc = NULL;
        g_gs = NULL;
        g_ms = NULL;
    }
    unlink_sqlite_db(g_goal_db_path);
    unlink_sqlite_db(g_msg_db_path);
    app_home_cleanup();
}

static void create_goal(const char *name, char *out_id) {
    goal_store_insert(g_gs, name, "test goal", "{\"done\": true}", "test-queue", out_id);
}

static int64_t now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

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

static void await_exit(int read_fd) {
    char byte;
    while (read(read_fd, &byte, 1) > 0) { }
    close(read_fd);
}

/* ========================================================================
 * stderr capture
 * ======================================================================== */

static int g_saved_stderr = -1;
static char g_capture_path[64];
static char g_stderr_buf[4096];

static void capture_stderr_begin(void) {
    fflush(stderr);
    g_saved_stderr = dup(STDERR_FILENO);
    snprintf(g_capture_path, sizeof(g_capture_path), "/tmp/test_oneshot_wait_err.XXXXXX");
    int fd = mkstemp(g_capture_path);
    TEST_ASSERT_TRUE(fd >= 0);
    dup2(fd, STDERR_FILENO);
    close(fd);
}

static const char *capture_stderr_end(void) {
    fflush(stderr);
    dup2(g_saved_stderr, STDERR_FILENO);
    close(g_saved_stderr);
    g_saved_stderr = -1;

    FILE *in = fopen(g_capture_path, "r");
    TEST_ASSERT_NOT_NULL(in);
    size_t n = fread(g_stderr_buf, 1, sizeof(g_stderr_buf) - 1, in);
    g_stderr_buf[n] = '\0';
    fclose(in);
    remove(g_capture_path);
    return g_stderr_buf;
}

/* ========================================================================
 * No goals created this run
 * ======================================================================== */

void test_no_goals_returns_immediately(void) {
    time_t started_after = time(NULL);
    int rc = oneshot_wait_for_goals(&g_session, goal_project_key(), started_after, 50);
    TEST_ASSERT_EQUAL(0, rc);
}

/* A pre-existing goal (created before started_after) must never be waited
 * on, even if it is still running. */
void test_pre_existing_goal_not_waited_on(void) {
    char id[40];
    create_goal("pre-existing", id);
    goal_store_update_status(g_gs, id, GOAL_STATUS_ACTIVE);

    time_t started_after = time(NULL) + 2; /* strictly after the goal above */
    int rc = oneshot_wait_for_goals(&g_session, goal_project_key(), started_after, 50);
    TEST_ASSERT_EQUAL(0, rc);
}

/* ========================================================================
 * Completed goal
 * ======================================================================== */

void test_wait_returns_when_completed(void) {
    time_t started_after = time(NULL);
    char id[40];
    create_goal("todo-web-app", id);
    goal_store_update_status(g_gs, id, GOAL_STATUS_COMPLETED);

    int rc = oneshot_wait_for_goals(&g_session, goal_project_key(), started_after, 50);
    TEST_ASSERT_EQUAL(0, rc);
}

/* ========================================================================
 * Failed goal: exits 1, fails loud with the goal name and log path
 * ======================================================================== */

void test_wait_returns_1_on_failed_goal(void) {
    time_t started_after = time(NULL);
    char id[40];
    create_goal("todo-web-app", id);
    goal_store_update_summary(g_gs, id, "could not find a working test command");
    goal_store_update_status(g_gs, id, GOAL_STATUS_FAILED);

    capture_stderr_begin();
    int rc = oneshot_wait_for_goals(&g_session, goal_project_key(), started_after, 50);
    const char *err = capture_stderr_end();

    TEST_ASSERT_EQUAL(1, rc);
    TEST_ASSERT_NOT_NULL(strstr(err, "Goal todo-web-app failed"));

    char *log_path = orchestrator_supervisor_log_path(id);
    TEST_ASSERT_NOT_NULL(log_path);
    TEST_ASSERT_NOT_NULL(strstr(err, log_path));
    free(log_path);
}

/* ========================================================================
 * Dead supervisor is respawned, then the wait continues to the real outcome
 * ======================================================================== */

typedef struct {
    AgentSession *session;
    const char *project;
    time_t started_after;
    int poll_ms;
    int result;
} WaitThreadArgs;

static void *run_wait(void *arg) {
    WaitThreadArgs *a = (WaitThreadArgs *)arg;
    a->result = oneshot_wait_for_goals(a->session, a->project, a->started_after, a->poll_ms);
    return NULL;
}

void test_wait_respawns_dead_supervisor(void) {
    char project[256];
    snprintf(project, sizeof(project), "%s", goal_project_key());

    time_t started_after = time(NULL);
    char id[40];
    create_goal("todo-web-app", id);
    goal_store_update_status(g_gs, id, GOAL_STATUS_ACTIVE);

    int read_fd = -1;
    pid_t dead_pid = fork_sleeper_watched(&read_fd);
    TEST_ASSERT_TRUE(dead_pid > 0);
    goal_store_update_supervisor(g_gs, id, dead_pid, now_millis());
    kill(dead_pid, SIGKILL);
    await_exit(read_fd);

    WaitThreadArgs args = { &g_session, project, started_after, 50, -999 };
    pthread_t tid;
    TEST_ASSERT_EQUAL(0, pthread_create(&tid, NULL, run_wait, &args));

    /* Spin (no sleep) so the respawned process is caught and killed as fast
     * as possible: it is a re-exec of this very test binary, which would
     * otherwise run the whole suite (and spawn its own child) if left alive. */
    pid_t new_pid = 0;
    time_t deadline = time(NULL) + 5;
    while (new_pid == 0 && time(NULL) < deadline) {
        Goal *g = goal_store_get(g_gs, id);
        if (g != NULL) {
            if (g->supervisor_pid > 0 && g->supervisor_pid != dead_pid) {
                new_pid = g->supervisor_pid;
            }
            goal_free(g);
        }
        if (new_pid == 0) sched_yield();
    }

    TEST_ASSERT_TRUE(new_pid > 0);
    kill(new_pid, SIGKILL);
    waitpid(new_pid, NULL, 0);

    /* Simulate the respawned supervisor reaching the goal's real outcome. */
    goal_store_update_status(g_gs, id, GOAL_STATUS_COMPLETED);

    pthread_join(tid, NULL);
    TEST_ASSERT_EQUAL(0, args.result);
}

/* ========================================================================
 * Interrupt: returns 130 and prints a resume hint
 * ======================================================================== */

void test_wait_interrupt_prints_resume_hint(void) {
    time_t started_after = time(NULL);
    char id[40];
    create_goal("todo-web-app", id);
    goal_store_update_status(g_gs, id, GOAL_STATUS_ACTIVE);

    /* Programmatically trigger the interrupt before the wait even starts:
     * the first check inside the loop sees it pending and returns
     * immediately, so this never touches the (unset) supervisor pid. */
    TEST_ASSERT_EQUAL(0, interrupt_init());
    interrupt_handler_trigger();

    capture_stderr_begin();
    int rc = oneshot_wait_for_goals(&g_session, goal_project_key(), started_after, 50);
    const char *err = capture_stderr_end();

    TEST_ASSERT_EQUAL(130, rc);
    TEST_ASSERT_NOT_NULL(strstr(err, "todo-web-app"));
    TEST_ASSERT_NOT_NULL(strstr(err, "still running"));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_no_goals_returns_immediately);
    RUN_TEST(test_pre_existing_goal_not_waited_on);
    RUN_TEST(test_wait_returns_when_completed);
    RUN_TEST(test_wait_returns_1_on_failed_goal);
    RUN_TEST(test_wait_respawns_dead_supervisor);
    RUN_TEST(test_wait_interrupt_prints_resume_hint);

    return UNITY_END();
}
