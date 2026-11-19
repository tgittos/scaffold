/**
 * lib/orchestrator/goal_progress.h - Live goal progress events
 *
 * Supervisors publish short progress events (action started/finished,
 * phase/goal transitions) onto a per-project pub/sub channel
 * ("goals:<project>") so an interactive REPL running in the same project
 * can render them between prompts without polling the goal/action stores
 * directly.
 *
 * Built on top of lib/ipc/message_store.c's existing channel primitives:
 * the channel is created lazily on first publish/subscribe.
 */

#ifndef LIB_ORCHESTRATOR_GOAL_PROGRESS_H
#define LIB_ORCHESTRATOR_GOAL_PROGRESS_H

#include "../ipc/message_store.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Default retention for progress channel messages. The channel is created
 * non-persistent specifically so message_cleanup_channel_messages() can trim
 * it; callers (supervisor + REPL) invoke goal_progress_cleanup() on a timer
 * so channel_messages doesn't grow unboundedly across a long-lived goal.
 */
#define GOAL_PROGRESS_RETENTION_SECONDS (60 * 60)

/**
 * Build the progress channel name for a project ("goals:<project>").
 * When project is NULL or empty, falls back to "goals:default" so
 * unscoped callers still land on a single shared channel.
 * Caller owns the returned string and must free() it.
 */
char *goal_progress_channel_name(const char *project);

/**
 * Publish a progress event for `goal_name` on the project's progress
 * channel, creating the channel if it does not exist yet.
 *
 * `event` is a short machine-readable tag consumed by goal_progress_format()
 * to pick a display symbol, e.g.:
 *   "phase_started", "action_started", "action_finished", "action_failed",
 *   "goal_completed", "goal_failed"
 * `detail` is the human-readable text rendered after the symbol (may be
 * NULL, treated as empty).
 *
 * Returns 0 on success, -1 on error.
 */
int goal_progress_publish(message_store_t *store, const char *project,
                           const char *event, const char *goal_name,
                           const char *detail);

/**
 * Subscribe `subscriber_id` to the progress channel for `project`,
 * creating the channel first if needed. Idempotent.
 * Returns 0 on success, -1 on error.
 */
int goal_progress_subscribe(message_store_t *store, const char *project,
                             const char *subscriber_id);

/**
 * Format one progress event (the raw ChannelMessage content produced by
 * goal_progress_publish) into a single human-readable display line, e.g.:
 *   "\u25c6 todo-web-app: \u2713 Write tests/conftest.py (3/7)"
 *
 * Caller owns the returned string and must free() it.
 * Returns NULL on malformed input (not valid JSON, or missing goal_name).
 */
char *goal_progress_format(const char *json_event);

/**
 * Build the warning shown on REPL quit when goals are still running in the
 * background, e.g.:
 *   "2 goals still running in the background (todo-web-app, api). Run
 *    scaffold here and /goals to check on them."
 *
 * `names` has `count` entries (goal names); order is preserved verbatim.
 * Caller owns the returned string and must free() it.
 * Returns NULL when count == 0 or names is NULL.
 */
char *goal_progress_quit_warning(const char **names, size_t count);

/**
 * Trim progress-channel messages older than `max_age_seconds`. Safe to call
 * frequently and from multiple processes (plain SQL DELETE); callers should
 * pass GOAL_PROGRESS_RETENTION_SECONDS unless they have a specific reason
 * not to. Returns the number of rows deleted, or -1 on error.
 */
int goal_progress_cleanup(message_store_t *store, int max_age_seconds);

#ifdef __cplusplus
}
#endif

#endif /* LIB_ORCHESTRATOR_GOAL_PROGRESS_H */
