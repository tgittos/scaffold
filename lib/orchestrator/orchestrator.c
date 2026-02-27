#include "orchestrator.h"
#include "../util/executable_path.h"
#include "../util/process_spawn.h"
#include "../util/debug_output.h"
#include <errno.h>
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

    char* args[] = {
        exe_path,
        "--supervisor",
        "--goal", (char*)goal_id,
        "--phase", (char*)phase_str,
        "--yolo",
        NULL
    };

    pid_t pid;
    if (process_spawn_devnull(args, &pid) != 0) {
        free(exe_path);
        return -1;
    }

    free(exe_path);

    int64_t started_at = now_millis();
    int rc = goal_store_update_supervisor(store, goal_id, pid, started_at);
    if (rc != 0) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    debug_printf("orchestrator: spawned supervisor pid=%d for goal %s (phase=%s)\n",
                 pid, goal_id, phase_str);
    return 0;
}

bool orchestrator_supervisor_alive(goal_store_t* store, const char* goal_id) {
    if (store == NULL || goal_id == NULL) return false;

    Goal* goal = goal_store_get(store, goal_id);
    if (goal == NULL) return false;

    pid_t pid = goal->supervisor_pid;
    goal_free(goal);

    if (pid <= 0) return false;

    if (kill(pid, 0) == 0) return true;

    if (errno == ESRCH) {
        goal_store_update_supervisor(store, goal_id, 0, 0);
    }
    return false;
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
            debug_printf("orchestrator: reaped supervisor pid=%d for goal %s "
                         "(exit=%d)\n", pid, goals[i]->id,
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            goal_store_update_supervisor(store, goals[i]->id, 0, 0);
        } else if (result == -1) {
            if (kill(pid, 0) != 0 && errno == ESRCH) {
                debug_printf("orchestrator: supervisor pid=%d for goal %s "
                             "no longer exists\n", pid, goals[i]->id);
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

    debug_printf("orchestrator: killed supervisor pid=%d for goal %s\n",
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
            debug_printf("orchestrator: clearing dead supervisor pid=%d "
                         "for goal %s\n", pid, goals[i]->id);
            goal_store_update_supervisor(store, goals[i]->id, 0, 0);
        }
    }

    goal_free_list(goals, count);
}

int orchestrator_respawn_dead(goal_store_t* store) {
    if (store == NULL) return -1;

    int respawned = 0;

    GoalStatus respawn_statuses[] = { GOAL_STATUS_PLANNING, GOAL_STATUS_ACTIVE };
    for (int s = 0; s < 2; s++) {
        size_t count = 0;
        Goal** goals = goal_store_list_by_status(store, respawn_statuses[s], &count);
        if (goals == NULL) continue;

        for (size_t i = 0; i < count; i++) {
            if (goals[i]->supervisor_pid != 0) continue;

            debug_printf("orchestrator: respawning supervisor for goal %s (%s, status=%s)\n",
                         goals[i]->id, goals[i]->name,
                         goal_status_to_string(goals[i]->status));
            if (orchestrator_spawn_supervisor(store, goals[i]->id) == 0) {
                respawned++;
            }
        }

        goal_free_list(goals, count);
    }

    return respawned;
}
