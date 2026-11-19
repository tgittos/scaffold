#define LOG_MODULE     LOG_MOD_GOAP
#define LOG_MODULE_STR "goap"
#include "../util/log.h"
#include "orchestrator.h"
#include "../util/app_home.h"
#include "../util/executable_path.h"
#include "../util/process_spawn.h"
#include <errno.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int64_t now_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * Does this PID belong to a running process?
 *
 * A zombie still answers kill(pid, 0), so children are probed with
 * waitpid(WNOHANG) as well — which reaps them, exactly as
 * orchestrator_reap_supervisors would.
 */
static bool pid_is_running(pid_t pid) {
    if (pid <= 0) return false;
    if (kill(pid, 0) != 0) {
        /* EPERM means the process exists but belongs to another user; only
         * ESRCH means it is gone. */
        return errno == EPERM;
    }
    if (waitpid(pid, NULL, WNOHANG) == pid) return false;
    return true;
}

char* orchestrator_supervisor_log_path(const char* goal_id) {
    if (goal_id == NULL) return NULL;

    char relative[128];
    snprintf(relative, sizeof(relative), "logs/supervisor-%s.log", goal_id);
    return app_home_path(relative);
}

/* Create the log directory; returns the log path (caller frees) or NULL. */
static char* prepare_log_path(const char* goal_id) {
    app_home_ensure_exists();

    char* log_dir = app_home_path("logs");
    if (log_dir == NULL) return NULL;
    mkdir(log_dir, 0700);
    free(log_dir);

    return orchestrator_supervisor_log_path(goal_id);
}

int orchestrator_spawn_supervisor(goal_store_t* store, const char* goal_id) {
    if (store == NULL || goal_id == NULL) return -1;

    Goal* goal = goal_store_get(store, goal_id);
    if (goal == NULL) return -1;

    const char* phase_str = (goal->status == GOAL_STATUS_PLANNING) ? "plan" : "execute";
    goal_free(goal);

    char* exe_path = get_executable_path();
    if (exe_path == NULL) {
        fprintf(stderr, "orchestrator: failed to get executable path\n");
        return -1;
    }

    char* log_path = prepare_log_path(goal_id);
    if (log_path == NULL) {
        fprintf(stderr, "orchestrator: failed to build supervisor log path\n");
        free(exe_path);
        return -1;
    }

    char* args[] = {
        exe_path,
        "--supervisor",
        "--goal", (char*)goal_id,
        "--phase", (char*)phase_str,
        "--log", "info",
        NULL
    };

    pid_t pid = process_spawn_logged(args, log_path);
    if (pid <= 0) {
        fprintf(stderr, "orchestrator: failed to spawn supervisor (log %s)\n",
                log_path);
        free(log_path);
        free(exe_path);
        return -1;
    }

    free(exe_path);

    int64_t started_at = now_millis();
    int rc = goal_store_update_supervisor(store, goal_id, pid, started_at);
    if (rc != 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        free(log_path);
        return -1;
    }

    LOG_INFO("orchestrator: spawned supervisor pid=%d for goal %s (phase=%s, log=%s)",
                 pid, goal_id, phase_str, log_path);
    free(log_path);
    return 0;
}

bool orchestrator_supervisor_alive(const Goal* g, time_t now) {
    if (g == NULL) return false;
    if (!pid_is_running(g->supervisor_pid)) return false;
    return (now - g->supervisor_heartbeat_at) < SUPERVISOR_HEARTBEAT_TIMEOUT_SEC;
}

void orchestrator_reap_supervisors(goal_store_t* store) {
    if (store == NULL) return;

    size_t count = 0;
    Goal** goals = goal_store_list_all(store, &count);
    if (goals == NULL) return;

    for (size_t i = 0; i < count; i++) {
        pid_t pid = goals[i]->supervisor_pid;
        if (pid <= 0) continue;

        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            LOG_INFO("orchestrator: reaped supervisor pid=%d for goal %s "
                         "(exit=%d)", pid, goals[i]->id,
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            goal_store_update_supervisor(store, goals[i]->id, 0, 0);
        } else if (result == -1) {
            if (kill(pid, 0) != 0 && errno == ESRCH) {
                LOG_DEBUG("orchestrator: supervisor pid=%d for goal %s "
                             "no longer exists", pid, goals[i]->id);
                goal_store_update_supervisor(store, goals[i]->id, 0, 0);
            }
        }
    }

    goal_free_list(goals, count);
}

int orchestrator_kill_supervisor(goal_store_t* store, const char* goal_id) {
    if (store == NULL || goal_id == NULL) return -1;

    Goal* goal = goal_store_get(store, goal_id);
    if (goal == NULL) return -1;

    pid_t pid = goal->supervisor_pid;
    goal_free(goal);

    if (pid <= 0) return -1;

    if (kill(pid, SIGTERM) != 0) {
        if (errno == ESRCH) {
            goal_store_update_supervisor(store, goal_id, 0, 0);
            return 0;
        }
        return -1;
    }

    usleep(100000);

    int status;
    pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == 0) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    goal_store_update_supervisor(store, goal_id, 0, 0);
    goal_store_update_status(store, goal_id, GOAL_STATUS_PAUSED);

    LOG_INFO("orchestrator: killed supervisor pid=%d for goal %s",
                 pid, goal_id);
    return 0;
}

void orchestrator_check_stale(goal_store_t* store) {
    if (store == NULL) return;

    size_t count = 0;
    Goal** goals = goal_store_list_all(store, &count);
    if (goals == NULL) return;

    for (size_t i = 0; i < count; i++) {
        pid_t pid = goals[i]->supervisor_pid;
        if (pid <= 0) continue;

        if (kill(pid, 0) != 0 && errno == ESRCH) {
            LOG_DEBUG("orchestrator: clearing dead supervisor pid=%d "
                         "for goal %s", pid, goals[i]->id);
            goal_store_update_supervisor(store, goals[i]->id, 0, 0);
        }
    }

    goal_free_list(goals, count);
}

int orchestrator_respawn_dead(goal_store_t* store, const char* project) {
    if (store == NULL || project == NULL) return -1;

    size_t count = 0;
    Goal** goals = goal_store_list_by_project(store, project, &count);
    if (goals == NULL) return 0;

    int respawned = 0;
    time_t now = time(NULL);
    for (size_t i = 0; i < count; i++) {
        if (goals[i]->status != GOAL_STATUS_PLANNING &&
            goals[i]->status != GOAL_STATUS_ACTIVE) {
            continue;
        }
        if (orchestrator_supervisor_alive(goals[i], now)) continue;

        /* A live process with a stale heartbeat is busy or wedged, not
         * gone: starting a second supervisor would duplicate its work. */
        if (pid_is_running(goals[i]->supervisor_pid)) {
            LOG_WARN("orchestrator: supervisor pid=%d for goal %s is stalled "
                         "(no heartbeat for %llds), not respawning",
                         goals[i]->supervisor_pid, goals[i]->id,
                         (long long)(now - goals[i]->supervisor_heartbeat_at));
            continue;
        }

        if (goals[i]->supervisor_pid != 0) {
            goal_store_update_supervisor(store, goals[i]->id, 0, 0);
        }

        LOG_INFO("orchestrator: respawning supervisor for goal %s (%s, status=%s)",
                     goals[i]->id, goals[i]->name,
                     goal_status_to_string(goals[i]->status));
        if (orchestrator_spawn_supervisor(store, goals[i]->id) == 0) {
            respawned++;
        }
    }

    goal_free_list(goals, count);
    return respawned;
}
