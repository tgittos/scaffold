#ifndef SUPERVISOR_H
#define SUPERVISOR_H

#include "../agent/session.h"
#include "services/services.h"
#include <stdbool.h>

#define SUPERVISOR_EXIT_COMPLETE    0
#define SUPERVISOR_EXIT_ERROR      -1
#define SUPERVISOR_EXIT_CONTEXT    -3

typedef enum {
    SUPERVISOR_PHASE_PLAN    = 0,
    SUPERVISOR_PHASE_EXECUTE = 1
} SupervisorPhase;

/** Consecutive idle timeout wakes before the model is re-prompted. */
#define SUPERVISOR_IDLE_WAKES_BEFORE_REPROMPT 2

/** Idle re-prompt bookkeeping for one phase. Zero-initialise before use. */
typedef struct {
    int idle_wakes;              /* consecutive idle timeout wakes with no work */
    int unproductive_reprompts;  /* re-prompts that produced no work */
} SupervisorIdle;

typedef enum {
    SUPERVISOR_IDLE_WAIT = 0,  /* keep waiting */
    SUPERVISOR_IDLE_REPROMPT,  /* nudge the model with the current world state */
    SUPERVISOR_IDLE_GIVE_UP    /* re-prompting is not making progress */
} SupervisorIdleAction;

/**
 * Decide what an idle supervisor should do on this wake.
 *
 * Only idle timeout wakes count toward a re-prompt: a notification wake means
 * something reported in, so it never shortens the idle window. Work in flight
 * (or produced by the last re-prompt) resets the whole tracker. Re-prompts that
 * produce nothing are capped at max_reprompts so a model that never creates
 * work cannot be prompted forever.
 *
 * @param idle  Tracker, updated in place
 * @param timeout_wake  True when the idle wait expired with no fd ready
 * @param work_in_flight  True when the goal has work running (or, in the plan
 *                        phase, actions to execute)
 * @param max_reprompts  Budget of unproductive re-prompts for this phase
 */
SupervisorIdleAction supervisor_idle_next(SupervisorIdle *idle, bool timeout_wake,
                                           bool work_in_flight, int max_reprompts);

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
 * Resolve the actual phase to use for a supervisor invocation.
 *
 * If the requested phase is PLAN but the goal is in PLANNING status and
 * already has existing actions (plan completed in a prior session), promotes
 * the goal to ACTIVE and returns SUPERVISOR_PHASE_EXECUTE instead.
 * This is the shared helper used by both supervisor_run and execute_start_goal.
 *
 * @param services  Services container
 * @param goal_id   UUID of the goal
 * @param requested The phase the caller requested
 * @return The phase to actually use
 */
SupervisorPhase supervisor_resolve_phase(Services *services, const char *goal_id,
                                          SupervisorPhase requested);

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
