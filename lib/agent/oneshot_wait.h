/* lib/agent/oneshot_wait.h - One-shot invocations wait for their goals.
 *
 * A one-shot run (`scaffold "build a todo app"` with no --phase) may create
 * goals that keep running in the background via a supervisor process after
 * the turn's LLM call returns. Without this, the one-shot process exits
 * immediately and the hero example never actually finishes.
 *
 * oneshot_wait_for_goals() polls the goals created during the run to a
 * terminal state (completed/failed), respawning dead supervisors along the
 * way (lib/orchestrator/orchestrator.c's liveness/respawn from Task 5.2) and
 * rendering progress lines from the goals:<project> channel (Task 5.3).
 */
#ifndef LIB_AGENT_ONESHOT_WAIT_H
#define LIB_AGENT_ONESHOT_WAIT_H

#include "session.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wait for all goals of `project` created after `started_after` to reach a
 * terminal state (completed or failed), printing progress along the way.
 *
 * Goals that already existed before `started_after` are never waited on.
 * If the run created no goals, returns immediately without polling.
 *
 * In --json mode, progress and the final summary are emitted as JSON system
 * events on stdout (via json_output_system); otherwise they go to stderr.
 * stdout never carries anything but JSON lines in --json mode.
 *
 * A dead supervisor for a still-active goal is respawned via
 * orchestrator_respawn_dead() so the wait is bounded by the goal's own
 * lifecycle (the supervisor's bounded idle re-prompt / give-up from Task
 * 5.2), not by an arbitrary timeout here.
 *
 * @param session        The one-shot session (must have services wired)
 * @param project        Project scope key (see goal_project_key())
 * @param started_after  Only goals created at or after this time are waited on
 * @param poll_ms        Poll interval in milliseconds between status checks
 * @return 0 if every tracked goal completed, 1 if any failed (prints
 *         "Goal <name> failed: <reason>" and the supervisor log path to
 *         stderr/JSON for each), 130 if interrupted (prints a resume hint)
 */
int oneshot_wait_for_goals(AgentSession* session, const char* project,
                            time_t started_after, int poll_ms);

#ifdef __cplusplus
}
#endif

#endif /* LIB_AGENT_ONESHOT_WAIT_H */
