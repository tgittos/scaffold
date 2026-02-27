#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "../agent/session.h"
#include "services/services.h"

#define SUPERVISOR_EXIT_COMPLETE    0
#define SUPERVISOR_EXIT_ERROR      -1
#define SUPERVISOR_EXIT_CONTEXT    -3

typedef enum {
    SUPERVISOR_PHASE_PLAN    = 0,
    SUPERVISOR_PHASE_EXECUTE = 1
} SupervisorPhase;

/**
 * Recover orphaned RUNNING actions for a goal.
 *
 * Checks each RUNNING action's work_item_id against the work queue:
 * - COMPLETED work item → mark action COMPLETED with work item result
 * - FAILED work item → mark action FAILED
 * - PENDING or missing → reset action to PENDING
 *
 * @param services  Services container with goal/action stores
 * @param goal_id   UUID of the goal to recover
 * @return Number of actions recovered, or -1 on error
 */
int supervisor_recover_orphaned_actions(Services *services, const char *goal_id);

/**
 * Run the supervisor event loop for a goal.
 *
 * The supervisor drives a goal to completion by calling GOAP tools
 * through the LLM. It operates as a headless REPL: no stdin, just
 * message poller notifications from worker completions.
 *
 * Lifecycle:
 *   1. Build initial status message from goal state
 *   2. session_process_message() — LLM examines state, calls GOAP tools
 *   3. select() on message poller fd (no stdin)
 *   4. Worker completion → inject notification → session_continue()
 *   5. Repeat 3-4 until goap_check_complete returns true
 *
 * @param session  Initialized session with services wired and message polling started
 * @param goal_id  UUID of the goal to supervise
 * @param phase    SUPERVISOR_PHASE_PLAN or SUPERVISOR_PHASE_EXECUTE
 * @return SUPERVISOR_EXIT_COMPLETE (0) on goal completion,
 *         SUPERVISOR_EXIT_ERROR (-1) on failure,
 *         SUPERVISOR_EXIT_CONTEXT (-3) if context window filled up (respawn needed)
 */
int supervisor_run(AgentSession *session, const char *goal_id, SupervisorPhase phase);

#endif /* SUPERVISOR_H */
