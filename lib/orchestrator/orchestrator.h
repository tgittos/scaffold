#ifndef ORCHESTRATOR_H
#define ORCHESTRATOR_H

#include "../db/goal_store.h"
#include <stdbool.h>
#include <time.h>

/** A supervisor whose last heartbeat is older than this is not alive. */
#define SUPERVISOR_HEARTBEAT_TIMEOUT_SEC 30

/**
 * Spawn a supervisor process for a goal.
 *
 * Forks the current executable with --supervisor --goal <id>.
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
 * A supervisor is alive when its PID exists, is not a zombie, and its last
 * heartbeat is less than SUPERVISOR_HEARTBEAT_TIMEOUT_SEC old. A zombie child
 * of this process is reaped as a side effect of the check.
 *
 * @param g  Goal to check (may be NULL)
 * @param now  Current time
 * @return true if the supervisor is running, false if dead, stalled or absent
 */
bool orchestrator_supervisor_alive(const Goal* g, time_t now);

/**
 * Build the path of a goal's supervisor log.
 *
 * @param goal_id  UUID of the goal
 * @return Newly allocated path (caller frees), or NULL on error
 */
char* orchestrator_supervisor_log_path(const char* goal_id);

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
 * Respawn supervisors for goals whose supervisor is gone.
 *
 * Finds PLANNING and ACTIVE goals of one project whose supervisor process no
 * longer exists (never started, exited, or killed) and spawns a new supervisor
 * for each. Goals of other projects — and goals written before project scoping,
 * whose project is NULL — are never touched. A goal whose supervisor process is
 * still running is left alone even when its heartbeat is stale, so a busy
 * supervisor never gets a twin.
 *
 * @param store  Goal store
 * @param project  Project scope key (see goal_project_key())
 * @return Number of supervisors respawned, -1 on error
 */
int orchestrator_respawn_dead(goal_store_t* store, const char* project);

#endif /* ORCHESTRATOR_H */
