#include "supervisor.h"
#include "goap_state.h"
#include "../ipc/message_poller.h"
#include "workflow/workflow.h"
#include "../ipc/notification_formatter.h"
#include "../session/conversation_tracker.h"
#include "../tools/subagent_tool.h"
#include "../util/debug_output.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include <cJSON.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static volatile sig_atomic_t g_supervisor_running = 1;

static void supervisor_signal_handler(int sig) {
    (void)sig;
    g_supervisor_running = 0;
}

/**
 * Build a status summary from current goal state for the initial LLM kick.
 * Caller owns the returned string.
 */
static char *build_initial_message(AgentSession *session, const char *goal_id) {
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

    size_t buf_size = 2048 + strlen(goal->name)
        + (goal->description ? strlen(goal->description) : 0)
        + (goal->goal_state ? strlen(goal->goal_state) : 0)
        + (goal->world_state ? strlen(goal->world_state) : 0);
    char *msg = malloc(buf_size);
    if (msg == NULL) goto cleanup;

    snprintf(msg, buf_size,
        "You are supervising goal \"%s\" (ID: %s).\n\n"
        "Description: %s\n\n"
        "Goal state (acceptance criteria):\n%s\n\n"
        "Current world state:\n%s\n\n"
        "Progress: %d/%d assertions satisfied.\n"
        "Actions: %d pending, %d running, %d completed, %d failed (total: %zu).\n\n"
        "Use GOAP tools to progress this goal to completion:\n"
        "1. Check for ready actions with goap_list_actions (status=\"pending\")\n"
        "2. Decompose ready compound actions into children with goap_create_actions\n"
        "3. Dispatch ready primitive actions to workers with goap_dispatch_action\n"
        "4. When workers complete, verify their effects with goap_get_action_results\n"
        "5. Update world state with goap_update_world_state for verified effects\n"
        "6. Check goal completion with goap_check_complete\n\n"
        "Begin by examining the current state and taking the next appropriate action.",
        goal->name, goal_id,
        goal->description ? goal->description : "(none)",
        goal->goal_state ? goal->goal_state : "{}",
        goal->world_state ? goal->world_state : "{}",
        progress.satisfied, progress.total,
        pending, running, completed, failed, action_count);

cleanup:
    action_free_list(actions, action_count);
    goal_free(goal);
    return msg;
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
            debug_printf("Supervisor: processing %d incoming messages\n", total_count);
            append_conversation_message(&session->session_data.conversation,
                                        "system", notification_text);
            int rc = session_continue(session);
            if (rc != 0) {
                debug_printf("Supervisor: session_continue returned %d\n", rc);
                result = SUPERVISOR_EXIT_ERROR;
            }
            free(notification_text);
        }
    }

    notification_bundle_destroy(bundle);
    return result;
}

/**
 * Rebuild the fd_set with message poller and approval channel fds.
 * Returns the max fd value.
 */
static int rebuild_fd_set(AgentSession *session, fd_set *read_fds, int notify_fd) {
    FD_ZERO(read_fds);

    int max_fd = -1;
    if (notify_fd >= 0) {
        FD_SET(notify_fd, read_fds);
        max_fd = notify_fd;
    }

    SubagentManager *mgr = &session->subagent_manager;
    for (size_t i = 0; i < mgr->subagents.count; i++) {
        Subagent *sub = &mgr->subagents.data[i];
        if (sub->status == SUBAGENT_STATUS_RUNNING &&
            sub->approval_channel.request_fd > 2) {
            FD_SET(sub->approval_channel.request_fd, read_fds);
            if (sub->approval_channel.request_fd > max_fd) {
                max_fd = sub->approval_channel.request_fd;
            }
        }
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
            case WORK_ITEM_ASSIGNED:
                /* Worker is actively processing — leave as RUNNING */
                break;
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

int supervisor_run(AgentSession *session, const char *goal_id) {
    if (session == NULL || goal_id == NULL) return SUPERVISOR_EXIT_ERROR;

    g_supervisor_running = 1;

    struct sigaction sa;
    sa.sa_handler = supervisor_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    debug_printf("Supervisor started for goal %s\n", goal_id);

    int orphans = supervisor_recover_orphaned_actions(session->services, goal_id);
    if (orphans > 0) {
        debug_printf("Supervisor: recovered %d orphaned actions\n", orphans);
    }

    char *initial_msg = build_initial_message(session, goal_id);
    if (initial_msg == NULL) {
        fprintf(stderr, "Supervisor: failed to build initial message for goal %s\n", goal_id);
        return SUPERVISOR_EXIT_ERROR;
    }

    int rc = session_process_message(session, initial_msg);
    free(initial_msg);

    if (rc != 0) {
        fprintf(stderr, "Supervisor: initial session_process_message failed (%d)\n", rc);
        return SUPERVISOR_EXIT_ERROR;
    }

    if (goal_is_complete(session, goal_id)) {
        debug_printf("Supervisor: goal %s complete after initial processing\n", goal_id);
        goal_store_t *gs = services_get_goal_store(session->services);
        if (gs != NULL)
            goal_store_update_status(gs, goal_id, GOAL_STATUS_COMPLETED);
        return SUPERVISOR_EXIT_COMPLETE;
    }

    int notify_fd = -1;
    if (session->message_poller != NULL) {
        notify_fd = message_poller_get_notify_fd(session->message_poller);
    }

    int consecutive_errors = 0;
    const int max_consecutive_errors = 3;

    while (g_supervisor_running) {
        fd_set read_fds;
        int max_fd = rebuild_fd_set(session, &read_fds, notify_fd);

        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);

        if (ready < 0) {
            if (!g_supervisor_running) break;
            continue;
        }

        int changes = subagent_poll_all(&session->subagent_manager);
        if (changes > 0) {
            debug_printf("Supervisor: %d subagent state changes detected\n", changes);
        }

        /* Handle approval requests from subagent workers */
        SubagentManager *mgr = &session->subagent_manager;
        for (size_t i = 0; i < mgr->subagents.count; i++) {
            Subagent *sub = &mgr->subagents.data[i];
            if (sub->status == SUBAGENT_STATUS_RUNNING &&
                sub->approval_channel.request_fd > 2 &&
                FD_ISSET(sub->approval_channel.request_fd, &read_fds)) {
                subagent_handle_approval_request(mgr, (int)i,
                                                 &session->gate_config);
            }
        }

        /* Handle message poller notifications */
        if (notify_fd >= 0 && ready > 0 && FD_ISSET(notify_fd, &read_fds)) {
            message_poller_clear_notification(session->message_poller);
            rc = process_notifications(session);
            if (rc != 0) {
                consecutive_errors++;
                if (consecutive_errors >= max_consecutive_errors) {
                    fprintf(stderr, "Supervisor: too many consecutive errors, exiting\n");
                    return SUPERVISOR_EXIT_ERROR;
                }
            } else {
                consecutive_errors = 0;
            }
        }

        /* On timeout with subagent changes, check for notifications
         * that arrived between the poller cycle and the select wake */
        if (changes > 0 && !(notify_fd >= 0 && ready > 0 &&
                             FD_ISSET(notify_fd, &read_fds))) {
            rc = process_notifications(session);
            if (rc != 0) {
                consecutive_errors++;
                if (consecutive_errors >= max_consecutive_errors) {
                    fprintf(stderr, "Supervisor: too many consecutive errors, exiting\n");
                    return SUPERVISOR_EXIT_ERROR;
                }
            } else {
                consecutive_errors = 0;
            }
        }

        if (goal_is_complete(session, goal_id)) {
            debug_printf("Supervisor: goal %s complete\n", goal_id);
            goal_store_t *gs2 = services_get_goal_store(session->services);
            if (gs2 != NULL)
                goal_store_update_status(gs2, goal_id, GOAL_STATUS_COMPLETED);
            return SUPERVISOR_EXIT_COMPLETE;
        }
    }

    debug_printf("Supervisor: shutting down (signal received)\n");
    return SUPERVISOR_EXIT_ERROR;
}
