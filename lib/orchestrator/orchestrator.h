#ifndef ORCHESTRATOR_H
#define ORCHESTRATOR_H

#include "../db/goal_store.h"
#include <stdbool.h>

/**
 * Spawn a supervisor process for a goal.
 *
 * Forks the current executable with --supervisor --goal <id> --yolo.
 * Stores the PID and start timestamp in the goal store.
 *
 * @param store  Goal store for PID persistence
 * @param goal_id  UUID of the goal to supervise
 * @return 0 on success, -1 on error
 */
int orchestrator_spawn_supervisor(goal_store_t* store, const char* goal_id);

/**
 * Check if a goal's supervisor is still alive.
 *
 * Uses kill(pid, 0) to test process existence. If the supervisor is
 * dead, clears the stale PID in the goal store.
 *
 * @param store  Goal store
 * @param goal_id  UUID of the goal
 * @return true if supervisor is running, false if dead or no supervisor
 */
bool orchestrator_supervisor_alive(goal_store_t* store, const char* goal_id);

/**
 * Reap zombie supervisor processes.
 *
 * Scans all active goals for dead supervisors using waitpid(WNOHANG).
 * Clears stale PIDs in the goal store. Call periodically from the
 * scaffold's REPL loop.
 *
 * @param store  Goal store
 */
void orchestrator_reap_supervisors(goal_store_t* store);

/**
 * Kill a supervisor and wait for it to exit.
 *
 * Sends SIGTERM, waits 100ms, then SIGKILL if still alive.
 * Clears the PID in the goal store and updates status to PAUSED.
 *
 * @param store  Goal store
 * @param goal_id  UUID of the goal whose supervisor to kill
 * @return 0 on success, -1 if no supervisor running or error
 */
int orchestrator_kill_supervisor(goal_store_t* store, const char* goal_id);

/**
 * Check for stale supervisors on startup.
 *
 * Scans goals with non-zero supervisor_pid. If the process doesn't
 * exist (kill returns ESRCH), clears the PID.
 *
 * @param store  Goal store
 */
void orchestrator_check_stale(goal_store_t* store);

/**
 * Respawn supervisors for active goals that have no running supervisor.
 *
 * Finds all ACTIVE goals with supervisor_pid == 0 and spawns a new
 * supervisor for each.
 *
 * @param store  Goal store
 * @return Number of supervisors respawned, -1 on error
 */
int orchestrator_respawn_dead(goal_store_t* store);

#endif /* ORCHESTRATOR_H */
