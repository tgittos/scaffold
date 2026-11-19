#define LOG_MODULE     LOG_MOD_GOAP
#define LOG_MODULE_STR "goap"
#include "../util/log.h"
#include "supervisor.h"
#include "orchestrator.h"
#include "goap_state.h"
#include "../ipc/message_poller.h"
#include "goal_progress.h"
#include "workflow/workflow.h"
#include "../ipc/notification_formatter.h"
#include "../session/conversation_tracker.h"
#include "../tools/subagent_tool.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_supervisor_running = 1;

/** Seconds the idle wait blocks before waking to heartbeat and re-check. */
#define SUPERVISOR_IDLE_TIMEOUT_SEC 10

/** Bound on per-goal actions tracked for progress-event deltas. Goals with
 *  more actions than this simply stop getting new-action progress events
 *  (existing ones already tracked keep working) rather than growing memory
 *  unboundedly. */
#define SUPERVISOR_PROGRESS_SNAPSHOT_MAX 512

#define DEAD_WORKER_REASON "worker died without reporting a result"

static void supervisor_signal_handler(int sig) {
    (void)sig;
    g_supervisor_running = 0;
}

/**
 * Build initial message for the PLAN phase.
 * Instructs LLM to research, decompose into actions, and save a plan document.
 * Lists any existing actions so the model can extend rather than duplicate.
 * Caller owns the returned string.
 */
static char *build_planner_message(AgentSession *session, const char *goal_id) {
    goal_store_t *gs = services_get_goal_store(session->services);
    action_store_t *as = services_get_action_store(session->services);
    if (gs == NULL) return NULL;

    Goal *goal = goal_store_get(gs, goal_id);
    if (goal == NULL) return NULL;

    /* Collect any existing actions so the planner can extend them without
     * creating duplicates. */
    size_t existing_count = 0;
    Action **existing = NULL;
    if (as != NULL)
        existing = action_store_list_by_goal(as, goal_id, &existing_count);

    /* Build the existing-actions section (empty string when none). */
    size_t existing_section_len = 0;
    char *existing_section = NULL;
    if (existing_count > 0) {
        size_t sec_size = 128 + existing_count * 256;
        existing_section = malloc(sec_size);
        if (existing_section != NULL) {
            int w = snprintf(existing_section, sec_size,
                "\nExisting actions (%zu already created — do NOT recreate these):\n",
                existing_count);
            for (size_t i = 0; i < existing_count && w < (int)sec_size - 2; i++) {
                int n = snprintf(existing_section + w, sec_size - (size_t)w,
                    "- %s\n",
                    existing[i]->description ? existing[i]->description : "(no description)");
                if (n > 0) w += n;
            }
            existing_section_len = (size_t)w;
        }
    }
    if (existing != NULL) action_free_list(existing, existing_count);

    size_t buf_size = 2048 + strlen(goal->name) + strlen(goal_id)
        + (goal->description ? strlen(goal->description) : 0)
        + (goal->goal_state ? strlen(goal->goal_state) : 0)
        + existing_section_len;
    char *msg = malloc(buf_size);
    if (msg == NULL) {
        goal_free(goal);
        free(existing_section);
        return NULL;
    }

    int written = snprintf(msg, buf_size,
        "You are the PLANNER for goal \"%s\" (ID: %s).\n\n"
        "Description: %s\n\n"
        "Goal state (acceptance criteria):\n%s\n\n"
        "Your job is to research and plan how to achieve this goal, then:\n"
        "1. Decompose it into actions using goap(operation=\"create_actions\")\n"
        "2. Save your research findings and execution strategy using goap(operation=\"save_plan_document\")\n\n"
        "Do NOT dispatch actions or update world state. Focus only on planning.\n"
        "When you have created actions and saved a plan document, your work is done.",
        goal->name, goal_id,
        goal->description ? goal->description : "(none)",
        goal->goal_state ? goal->goal_state : "{}");

    if (existing_section != NULL && written > 0 && (size_t)written < buf_size) {
        strncat(msg, existing_section, buf_size - (size_t)written - 1);
        free(existing_section);
    } else {
        free(existing_section);
    }

    goal_free(goal);
    return msg;
}

/**
 * Build initial message for the EXECUTE phase.
 * Includes plan_document context and instructs LLM to dispatch/verify/update.
 * Caller owns the returned string.
 */
static char *build_executor_message(AgentSession *session, const char *goal_id) {
    goal_store_t *gs = services_get_goal_store(session->services);
    action_store_t *as = services_get_action_store(session->services);
    if (gs == NULL || as == NULL) return NULL;

    Goal *goal = goal_store_get(gs, goal_id);
    if (goal == NULL) return NULL;

    size_t action_count = 0;
    Action **actions = action_store_list_by_goal(as, goal_id, &action_count);

    int pending = 0, running = 0, completed = 0, failed = 0;
    for (size_t i = 0; i < action_count; i++) {
        switch (actions[i]->status) {
            case ACTION_STATUS_PENDING:   pending++;   break;
            case ACTION_STATUS_RUNNING:   running++;   break;
            case ACTION_STATUS_COMPLETED: completed++; break;
            case ACTION_STATUS_FAILED:    failed++;    break;
            case ACTION_STATUS_SKIPPED:   break;
        }
    }

    GoapProgress progress = goap_check_progress(goal->goal_state, goal->world_state);

    size_t buf_size = 2048 + strlen(goal->name) + strlen(goal_id)
        + (goal->description ? strlen(goal->description) : 0)
        + (goal->goal_state ? strlen(goal->goal_state) : 0)
        + (goal->world_state ? strlen(goal->world_state) : 0)
        + (goal->plan_document ? strlen(goal->plan_document) : 0)
        + (goal->summary ? strlen(goal->summary) : 0);
    char *msg = malloc(buf_size);
    if (msg == NULL) goto cleanup;

    int written = snprintf(msg, buf_size,
        "You are the EXECUTOR for goal \"%s\" (ID: %s).\n\n"
        "Description: %s\n\n",
        goal->name, goal_id,
        goal->description ? goal->description : "(none)");
    if (written < 0 || (size_t)written >= buf_size) goto cleanup_msg;

    if (goal->plan_document) {
        int n = snprintf(msg + written, buf_size - written,
            "Plan document (from planning phase):\n%s\n\n",
            goal->plan_document);
        if (n > 0) written += n;
    }

    if (goal->summary) {
        int n = snprintf(msg + written, buf_size - written,
            "Previous progress summary:\n%s\n\n",
            goal->summary);
        if (n > 0) written += n;
    }

    if ((size_t)written >= buf_size) goto cleanup_msg;

    snprintf(msg + written, buf_size - written,
        "Goal state (acceptance criteria):\n%s\n\n"
        "Current world state:\n%s\n\n"
        "Progress: %d/%d assertions satisfied.\n"
        "Actions: %d pending, %d running, %d completed, %d failed (total: %zu).\n\n"
        "Use the goap tool to progress this goal to completion:\n"
        "1. Check for ready actions with goap(operation=\"list_actions\", status=\"pending\")\n"
        "2. Decompose ready compound actions into children with goap(operation=\"create_actions\")\n"
        "3. Dispatch ready primitive actions to workers with goap(operation=\"dispatch_action\")\n"
        "4. When workers complete, verify their effects with goap(operation=\"get_action_results\")\n"
        "5. Update world state with goap(operation=\"update_world_state\") for verified effects\n"
        "6. Check goal completion with goap(operation=\"check_complete\")\n\n"
        "Begin by examining the current state and taking the next appropriate action.",
        goal->goal_state ? goal->goal_state : "{}",
        goal->world_state ? goal->world_state : "{}",
        progress.satisfied, progress.total,
        pending, running, completed, failed, action_count);

    goto cleanup;

cleanup_msg:
    free(msg);
    msg = NULL;
cleanup:
    action_free_list(actions, action_count);
    goal_free(goal);
    return msg;
}

/**
 * Check if the plan phase is complete: goal has a non-null plan_document
 * AND at least one action exists in the action store.
 */
static bool plan_is_complete(AgentSession *session, const char *goal_id) {
    goal_store_t *gs = services_get_goal_store(session->services);
    action_store_t *as = services_get_action_store(session->services);
    if (gs == NULL || as == NULL) return false;

    Goal *goal = goal_store_get(gs, goal_id);
    if (goal == NULL) return false;

    bool has_plan = (goal->plan_document != NULL && goal->plan_document[0] != '\0');
    goal_free(goal);
    if (!has_plan) return false;

    size_t action_count = 0;
    Action **actions = action_store_list_by_goal(as, goal_id, &action_count);
    if (actions != NULL) action_free_list(actions, action_count);

    return action_count > 0;
}

/**
 * Check if the goal is complete by inspecting world_state vs goal_state.
 * Pure C check — no LLM call.
 */
static bool goal_is_complete(AgentSession *session, const char *goal_id) {
    goal_store_t *gs = services_get_goal_store(session->services);
    if (gs == NULL) return false;

    Goal *goal = goal_store_get(gs, goal_id);
    if (goal == NULL) return false;

    if (goal->status == GOAL_STATUS_COMPLETED) {
        goal_free(goal);
        return true;
    }

    GoapProgress progress = goap_check_progress(goal->goal_state, goal->world_state);
    goal_free(goal);
    return progress.complete;
}

/**
 * Process pending notifications from the message poller.
 * Injects them into conversation history and calls session_continue().
 * Returns 0 on success, SUPERVISOR_EXIT_ERROR on error.
 */
static int process_notifications(AgentSession *session) {
    notification_bundle_t *bundle = notification_bundle_create(
        session->session_id, session->services);
    if (bundle == NULL) return 0;

    int result = 0;
    int total_count = notification_bundle_total_count(bundle);
    if (total_count > 0) {
        char *notification_text = notification_format_for_llm(bundle);
        if (notification_text != NULL) {
            LOG_INFO("Supervisor: processing %d incoming messages", total_count);
            append_conversation_message(&session->session_data.conversation,
                                        "system", notification_text);
            int rc = session_continue(session);
            if (rc == SESSION_CONTEXT_FULL) {
                LOG_WARN("Supervisor: session_continue returned context full");
                result = SUPERVISOR_EXIT_CONTEXT;
            } else if (rc != 0) {
                LOG_ERROR("Supervisor: session_continue returned %d", rc);
                result = SUPERVISOR_EXIT_ERROR;
            }
            free(notification_text);
        }
    }

    notification_bundle_destroy(bundle);
    return result;
}

/**
 * Record that this supervisor is still alive.
 */
static void supervisor_heartbeat(AgentSession *session, const char *goal_id) {
    goal_store_t *gs = services_get_goal_store(session->services);
    if (gs != NULL) goal_store_heartbeat(gs, goal_id, time(NULL));
}

/**
 * Is there work in flight for this goal?
 *
 * Execute phase: at least one action is RUNNING, so a worker will report back.
 * Plan phase: no worker ever runs, so "work" means the planner produced actions
 * for the execute phase to pick up.
 */
static bool work_in_flight(AgentSession *session, const char *goal_id,
                            SupervisorPhase phase) {
    action_store_t *as = services_get_action_store(session->services);
    if (as == NULL) return false;

    if (phase == SUPERVISOR_PHASE_PLAN) {
        return action_store_has_actions(as, goal_id);
    }
    return action_store_count_by_status(as, goal_id, ACTION_STATUS_RUNNING) > 0;
}

SupervisorIdleAction supervisor_idle_next(SupervisorIdle *idle, bool timeout_wake,
                                           bool work_in_flight_now, int max_reprompts) {
    if (idle == NULL) return SUPERVISOR_IDLE_WAIT;

    if (work_in_flight_now) {
        idle->idle_wakes = 0;
        idle->unproductive_reprompts = 0;
        return SUPERVISOR_IDLE_WAIT;
    }

    /* Notification wakes mean something reported in; they must not shorten the
     * idle window. */
    if (!timeout_wake) return SUPERVISOR_IDLE_WAIT;

    idle->idle_wakes++;
    if (idle->idle_wakes < SUPERVISOR_IDLE_WAKES_BEFORE_REPROMPT) {
        return SUPERVISOR_IDLE_WAIT;
    }

    idle->idle_wakes = 0;
    if (idle->unproductive_reprompts >= max_reprompts) {
        return SUPERVISOR_IDLE_GIVE_UP;
    }
    idle->unproductive_reprompts++;
    return SUPERVISOR_IDLE_REPROMPT;
}

/**
 * Rebuild the fd_set with the message poller fd.
 * Returns the max fd value.
 */
static int rebuild_fd_set(fd_set *read_fds, int notify_fd) {
    FD_ZERO(read_fds);

    int max_fd = -1;
    if (notify_fd >= 0) {
        FD_SET(notify_fd, read_fds);
        max_fd = notify_fd;
    }

    return max_fd;
}

int supervisor_recover_orphaned_actions(Services *services, const char *goal_id) {
    if (services == NULL || goal_id == NULL) return -1;

    action_store_t *as = services_get_action_store(services);
    goal_store_t *gs = services_get_goal_store(services);
    if (as == NULL || gs == NULL) return -1;

    Goal *goal = goal_store_get(gs, goal_id);
    if (goal == NULL) return -1;

    size_t count = 0;
    Action **running = action_store_list_running(as, goal_id, &count);
    if (running == NULL || count == 0) {
        goal_free(goal);
        if (running) action_free_list(running, count);
        return 0;
    }

    WorkQueue *wq = work_queue_create(goal->queue_name);
    goal_free(goal);
    if (wq == NULL) {
        action_free_list(running, count);
        return -1;
    }

    int recovered = 0;
    for (size_t i = 0; i < count; i++) {
        Action *a = running[i];

        if (a->work_item_id[0] == '\0') {
            action_store_update_status(as, a->id, ACTION_STATUS_PENDING, NULL);
            recovered++;
            continue;
        }

        WorkItem *wi = work_queue_get_item(wq, a->work_item_id);
        if (wi == NULL) {
            action_store_update_status(as, a->id, ACTION_STATUS_PENDING, NULL);
            recovered++;
            continue;
        }

        switch (wi->status) {
            case WORK_ITEM_COMPLETED:
                action_store_update_status(as, a->id, ACTION_STATUS_COMPLETED,
                                           wi->result);
                recovered++;
                break;
            case WORK_ITEM_FAILED:
                action_store_update_status(as, a->id, ACTION_STATUS_FAILED,
                                           wi->error);
                recovered++;
                break;
            case WORK_ITEM_ASSIGNED: {
                /* Recovery runs when a supervisor starts, before it dispatches
                 * anything, so this item was claimed by a worker of a previous
                 * supervisor that is no longer reporting. Hand it back to the
                 * queue (or fail it once its attempts run out) instead of
                 * letting it hold a worker slot forever. */
                work_queue_fail(wq, a->work_item_id, DEAD_WORKER_REASON);
                WorkItem *after = work_queue_get_item(wq, a->work_item_id);
                bool exhausted = (after != NULL && after->status == WORK_ITEM_FAILED);
                if (after != NULL) work_item_free(after);

                if (exhausted) {
                    action_store_update_status(as, a->id, ACTION_STATUS_FAILED,
                                               DEAD_WORKER_REASON);
                } else {
                    action_store_update_status(as, a->id, ACTION_STATUS_PENDING,
                                               NULL);
                }
                recovered++;
                break;
            }
            default:
                action_store_update_status(as, a->id, ACTION_STATUS_PENDING, NULL);
                recovered++;
                break;
        }
        work_item_free(wi);
    }

    work_queue_destroy(wq);
    action_free_list(running, count);
    return recovered;
}

/**
 * Handle context-full exit for any phase.
 * Saves a summary and returns SUPERVISOR_EXIT_CONTEXT.
 */
static int handle_context_full_exit(AgentSession *session, const char *goal_id,
                                     SupervisorPhase phase) {
    const char *phase_name = (phase == SUPERVISOR_PHASE_PLAN) ? "planning" : "execution";
    LOG_WARN("Supervisor: context full during %s", phase_name);
    goal_store_t *gs = services_get_goal_store(session->services);
    if (gs != NULL) {
        char summary[128];
        snprintf(summary, sizeof(summary),
                 "Context full during %s, respawn needed", phase_name);
        goal_store_update_summary(gs, goal_id, summary);
    }
    return SUPERVISOR_EXIT_CONTEXT;
}

/**
 * Re-prompt the model for the current phase with the latest world state.
 * Returns 0 on success, SUPERVISOR_EXIT_CONTEXT when the context filled up,
 * SUPERVISOR_EXIT_ERROR otherwise.
 */
static int reprompt_with_world_state(AgentSession *session, const char *goal_id,
                                      SupervisorPhase phase) {
    char *msg = (phase == SUPERVISOR_PHASE_PLAN)
        ? build_planner_message(session, goal_id)
        : build_executor_message(session, goal_id);
    if (msg == NULL) return SUPERVISOR_EXIT_ERROR;

    LOG_INFO("Supervisor: no workers running for goal %s, re-prompting with "
             "current world state", goal_id);
    int rc = session_process_message(session, msg);
    free(msg);

    if (rc == SESSION_CONTEXT_FULL) return SUPERVISOR_EXIT_CONTEXT;
    return (rc == 0) ? 0 : SUPERVISOR_EXIT_ERROR;
}

/**
 * Phase-aware completion check.
 * Plan phase: plan_is_complete → set goal ACTIVE, return SUPERVISOR_EXIT_COMPLETE
 *   (caller continues into execute in the same process).
 * Execute phase: goal_is_complete → mark COMPLETED, return SUPERVISOR_EXIT_COMPLETE.
 * Returns SUPERVISOR_EXIT_COMPLETE when done, -1 to continue.
 */
static int check_phase_completion(AgentSession *session, const char *goal_id,
                                   SupervisorPhase phase) {
    goal_store_t *gs = services_get_goal_store(session->services);

    if (phase == SUPERVISOR_PHASE_PLAN) {
        if (plan_is_complete(session, goal_id)) {
            LOG_INFO("Supervisor: plan complete for goal %s, activating", goal_id);
            if (gs != NULL)
                goal_store_update_status(gs, goal_id, GOAL_STATUS_ACTIVE);
            return SUPERVISOR_EXIT_COMPLETE;
        }
    } else {
        if (goal_is_complete(session, goal_id)) {
            LOG_INFO("Supervisor: goal %s complete", goal_id);
            if (gs != NULL)
                goal_store_update_status(gs, goal_id, GOAL_STATUS_COMPLETED);
            return SUPERVISOR_EXIT_COMPLETE;
        }
    }
    return -1;  /* not complete yet */
}

SupervisorPhase supervisor_resolve_phase(Services *services, const char *goal_id,
                                          SupervisorPhase requested) {
    if (services == NULL || goal_id == NULL) return requested;
    if (requested != SUPERVISOR_PHASE_PLAN) return requested;

    goal_store_t *gs = services_get_goal_store(services);
    action_store_t *as = services_get_action_store(services);
    if (gs == NULL || as == NULL) return requested;

    Goal *goal = goal_store_get(gs, goal_id);
    if (goal == NULL) return requested;

    bool is_planning = (goal->status == GOAL_STATUS_PLANNING);
    goal_free(goal);

    if (is_planning && action_store_has_actions(as, goal_id)) {
        LOG_INFO("Supervisor: PLANNING goal %s has existing actions, skipping plan phase",
                 goal_id);
        goal_store_update_status(gs, goal_id, GOAL_STATUS_ACTIVE);
        return SUPERVISOR_PHASE_EXECUTE;
    }

    return requested;
}

/** Tracks the last-seen status of one action, so progress events fire only
 *  on a transition rather than on every poll. */
typedef struct {
    char id[40];
    ActionStatus status;
} ProgressSnapshotEntry;

/**
 * Publish a goal-level progress event (phase/goal transitions, not tied to
 * a specific action). No-op when the message store is unavailable.
 */
static void supervisor_publish_goal_event(AgentSession *session, const char *project,
                                           const char *goal_name, const char *event,
                                           const char *detail) {
    message_store_t *ms = services_get_message_store(session->services);
    if (ms == NULL || goal_name[0] == '\0') return;
    goal_progress_publish(ms, project, event, goal_name, detail);
}

/**
 * Diff the goal's current actions against `snapshot` and publish a progress
 * event for each action that newly transitioned into RUNNING, COMPLETED, or
 * FAILED since the last call. Updates `snapshot` in place.
 */
static void supervisor_publish_action_events(AgentSession *session, const char *goal_id,
                                              const char *project, const char *goal_name,
                                              ProgressSnapshotEntry *snapshot,
                                              size_t *snapshot_count) {
    message_store_t *ms = services_get_message_store(session->services);
    action_store_t *as = services_get_action_store(session->services);
    if (ms == NULL || as == NULL || goal_name[0] == '\0') return;

    size_t count = 0;
    Action **actions = action_store_list_by_goal(as, goal_id, &count);
    if (actions == NULL) return;

    int completed = 0;
    for (size_t i = 0; i < count; i++) {
        if (actions[i]->status == ACTION_STATUS_COMPLETED) completed++;
    }

    for (size_t i = 0; i < count; i++) {
        Action *a = actions[i];

        ActionStatus prev = ACTION_STATUS_PENDING;
        bool known = false;
        size_t slot = *snapshot_count;
        for (size_t j = 0; j < *snapshot_count; j++) {
            if (strcmp(snapshot[j].id, a->id) == 0) {
                prev = snapshot[j].status;
                known = true;
                slot = j;
                break;
            }
        }
        if (!known && *snapshot_count < SUPERVISOR_PROGRESS_SNAPSHOT_MAX) {
            slot = (*snapshot_count)++;
            strncpy(snapshot[slot].id, a->id, sizeof(snapshot[slot].id) - 1);
            snapshot[slot].id[sizeof(snapshot[slot].id) - 1] = '\0';
        }
        if (slot < SUPERVISOR_PROGRESS_SNAPSHOT_MAX) snapshot[slot].status = a->status;

        if (known && prev == a->status) continue;
        if (!known && a->status == ACTION_STATUS_PENDING) continue;

        const char *desc = a->description ? a->description : "(action)";
        char detail[320];

        if (a->status == ACTION_STATUS_COMPLETED) {
            snprintf(detail, sizeof(detail), "%s (%d/%zu)", desc, completed, count);
            goal_progress_publish(ms, project, "action_finished", goal_name, detail);
        } else if (a->status == ACTION_STATUS_FAILED) {
            snprintf(detail, sizeof(detail), "%s (%d/%zu)", desc, completed, count);
            goal_progress_publish(ms, project, "action_failed", goal_name, detail);
        } else if (a->status == ACTION_STATUS_RUNNING) {
            snprintf(detail, sizeof(detail), "%s", desc);
            goal_progress_publish(ms, project, "action_started", goal_name, detail);
        }
    }

    action_free_list(actions, count);
}

int supervisor_run(AgentSession *session, const char *goal_id, SupervisorPhase phase) {
    if (session == NULL || goal_id == NULL) return SUPERVISOR_EXIT_ERROR;

    /* If the caller passes PLAN but the goal already has actions (e.g. plan was
     * done in a prior session and the supervisor is being restarted without
     * --phase execute), skip straight to execute. */
    phase = supervisor_resolve_phase(session->services, goal_id, phase);

    /* Progress events are published under the goal's name/project, fetched
     * once up front — both are immutable after goal creation. */
    char progress_goal_name[128] = {0};
    char progress_project[128] = {0};
    goal_store_t *progress_gs = services_get_goal_store(session->services);
    if (progress_gs != NULL) {
        Goal *progress_goal = goal_store_get(progress_gs, goal_id);
        if (progress_goal != NULL) {
            snprintf(progress_goal_name, sizeof(progress_goal_name), "%s", progress_goal->name);
            if (progress_goal->project != NULL) {
                snprintf(progress_project, sizeof(progress_project), "%s", progress_goal->project);
            }
            goal_free(progress_goal);
        }
    }
    ProgressSnapshotEntry progress_snapshot[SUPERVISOR_PROGRESS_SNAPSHOT_MAX];
    size_t progress_snapshot_count = 0;

    g_supervisor_running = 1;

    struct sigaction sa;
    sa.sa_handler = supervisor_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    /*
     * Phase loop: run PLAN then, in the same process, continue as EXECUTE.
     * When the plan phase completes, check_phase_completion sets the goal
     * ACTIVE and returns SUPERVISOR_EXIT_COMPLETE.  Instead of returning to
     * the OS we simply advance `phase` and loop — one decomposition, one
     * supervisor process.
     */
    for (;;) {
        LOG_INFO("Supervisor started for goal %s (phase=%s)", goal_id,
                     phase == SUPERVISOR_PHASE_PLAN ? "plan" : "execute");

        supervisor_publish_goal_event(session, progress_project, progress_goal_name,
                                       "phase_started",
                                       phase == SUPERVISOR_PHASE_PLAN ? "planning" : "executing");

        supervisor_heartbeat(session, goal_id);

        /* Recover orphaned running actions at the start of execute phase. */
        if (phase == SUPERVISOR_PHASE_EXECUTE) {
            int orphans = supervisor_recover_orphaned_actions(session->services, goal_id);
            if (orphans > 0) {
                LOG_INFO("Supervisor: recovered %d orphaned actions", orphans);
            }
        }

        char *initial_msg = (phase == SUPERVISOR_PHASE_PLAN)
            ? build_planner_message(session, goal_id)
            : build_executor_message(session, goal_id);

        if (initial_msg == NULL) {
            fprintf(stderr, "Supervisor: failed to build initial message for goal %s\n", goal_id);
            return SUPERVISOR_EXIT_ERROR;
        }

        int rc = session_process_message(session, initial_msg);
        free(initial_msg);

        if (rc == SESSION_CONTEXT_FULL) {
            return handle_context_full_exit(session, goal_id, phase);
        }

        if (rc != 0) {
            fprintf(stderr, "Supervisor: initial session_process_message failed (%d)\n", rc);
            return SUPERVISOR_EXIT_ERROR;
        }

        supervisor_publish_action_events(session, goal_id, progress_project, progress_goal_name,
                                          progress_snapshot, &progress_snapshot_count);

        int completion = check_phase_completion(session, goal_id, phase);
        if (completion == SUPERVISOR_EXIT_COMPLETE) {
            if (phase == SUPERVISOR_PHASE_PLAN) {
                /* Plan done in one shot — continue as executor. */
                LOG_INFO("Supervisor: plan complete (initial pass), continuing as executor for goal %s",
                         goal_id);
                phase = SUPERVISOR_PHASE_EXECUTE;
                continue;
            }
            supervisor_publish_goal_event(session, progress_project, progress_goal_name,
                                           "goal_completed", "all actions complete");
            return SUPERVISOR_EXIT_COMPLETE;
        }

        int notify_fd = -1;
        if (session->message_poller != NULL) {
            notify_fd = message_poller_get_notify_fd(session->message_poller);
        }

        int consecutive_errors = 0;
        /* Single failures may be transient notification issues; give up after 3 */
        const int max_consecutive_errors = 3;
        SupervisorIdle idle = { 0, 0 };
        bool plan_phase_complete = false;

        while (g_supervisor_running) {
            rc = 0;  /* Reset per iteration to avoid stale context-full signals */
            fd_set read_fds;
            int max_fd = rebuild_fd_set(&read_fds, notify_fd);

            struct timeval timeout;
            timeout.tv_sec = SUPERVISOR_IDLE_TIMEOUT_SEC;
            timeout.tv_usec = 0;

            int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

            supervisor_heartbeat(session, goal_id);

            if (ready < 0) {
                if (!g_supervisor_running) break;
                continue;
            }

            int changes = subagent_poll_all(&session->subagent_manager);
            if (changes > 0) {
                LOG_DEBUG("Supervisor: %d subagent state changes detected", changes);
            }

            if (notify_fd >= 0 && ready > 0 && FD_ISSET(notify_fd, &read_fds)) {
                message_poller_clear_notification(session->message_poller);
                rc = process_notifications(session);
            }

            /* On timeout with subagent changes, check for notifications
             * that arrived between the poller cycle and the select wake */
            if (changes > 0 && !(notify_fd >= 0 && ready > 0 &&
                                 FD_ISSET(notify_fd, &read_fds))) {
                rc = process_notifications(session);
            }

            if (rc == SUPERVISOR_EXIT_CONTEXT) {
                return handle_context_full_exit(session, goal_id, phase);
            }
            if (rc != 0) {
                consecutive_errors++;
                if (consecutive_errors >= max_consecutive_errors) {
                    fprintf(stderr, "Supervisor: too many consecutive errors, exiting\n");
                    return SUPERVISOR_EXIT_ERROR;
                }
            } else {
                consecutive_errors = 0;
            }

            /* Nothing running means nothing will report back: nudge the model
             * with the current world state rather than spinning on an empty
             * queue — but only after two idle timeouts, and only while the
             * re-prompt budget lasts. */
            SupervisorIdleAction idle_action = supervisor_idle_next(
                &idle, ready == 0, work_in_flight(session, goal_id, phase),
                max_consecutive_errors);

            if (idle_action == SUPERVISOR_IDLE_GIVE_UP) {
                fprintf(stderr, "Supervisor: no progress after %d re-prompts for goal %s, "
                                "exiting\n", max_consecutive_errors, goal_id);
                return SUPERVISOR_EXIT_ERROR;
            }

            if (idle_action == SUPERVISOR_IDLE_REPROMPT) {
                int reprompt_rc = reprompt_with_world_state(session, goal_id, phase);
                if (reprompt_rc == SUPERVISOR_EXIT_CONTEXT) {
                    return handle_context_full_exit(session, goal_id, phase);
                }
                if (reprompt_rc != 0) {
                    consecutive_errors++;
                    if (consecutive_errors >= max_consecutive_errors) {
                        fprintf(stderr, "Supervisor: too many consecutive errors, exiting\n");
                        return SUPERVISOR_EXIT_ERROR;
                    }
                } else {
                    consecutive_errors = 0;
                }
                supervisor_heartbeat(session, goal_id);
            }

            supervisor_publish_action_events(session, goal_id, progress_project, progress_goal_name,
                                              progress_snapshot, &progress_snapshot_count);

            completion = check_phase_completion(session, goal_id, phase);
            if (completion == SUPERVISOR_EXIT_COMPLETE) {
                if (phase == SUPERVISOR_PHASE_PLAN) {
                    /* Break inner loop to transition to execute phase. */
                    plan_phase_complete = true;
                    break;
                }
                supervisor_publish_goal_event(session, progress_project, progress_goal_name,
                                               "goal_completed", "all actions complete");
                return SUPERVISOR_EXIT_COMPLETE;
            }
        }

        if (!g_supervisor_running) {
            LOG_INFO("Supervisor: shutting down (signal received)");
            return SUPERVISOR_EXIT_ERROR;
        }

        if (plan_phase_complete) {
            LOG_INFO("Supervisor: plan complete (event loop), continuing as executor for goal %s",
                     goal_id);
            phase = SUPERVISOR_PHASE_EXECUTE;
            continue;
        }

        /* Should only reach here on signal exit (handled above) */
        break;
    }

    return SUPERVISOR_EXIT_ERROR;
}
