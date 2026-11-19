#define LOG_MODULE     LOG_MOD_AGENT
#define LOG_MODULE_STR "agent"
#include "../util/log.h"
#include "oneshot_wait.h"
#include "../db/goal_store.h"
#include "../orchestrator/orchestrator.h"
#include "../orchestrator/goal_progress.h"
#include "../ipc/message_store.h"
#include "../services/services.h"
#include "../ui/json_output.h"
#include "../util/interrupt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Subscriber id this wait registers under on the goals:<project> channel. */
#define ONESHOT_WAIT_SUBSCRIBER "oneshot"

/* Upper bound on goals tracked in one run; a single one-shot turn creating
 * more than this is not a real scenario and would just mean truncating the
 * wait rather than growing unboundedly. */
#define ONESHOT_WAIT_MAX_TRACKED 256

/* Default poll interval when the caller passes a non-positive value. */
#define ONESHOT_WAIT_DEFAULT_POLL_MS 500

typedef struct {
    char id[40];
    char name[128];
} TrackedGoal;

/* Route one rendered line to stdout-as-JSON (--json mode) or stderr
 * (human mode). stdout must never carry anything but JSON lines in --json
 * mode, so human-readable text always goes to stderr. */
static void emit_line(bool json_mode, const char* subtype, const char* line) {
    if (line == NULL) return;
    if (json_mode) {
        json_output_system(subtype, line);
    } else {
        fprintf(stderr, "%s\n", line);
    }
}

static void drain_progress(message_store_t* ms, const char* channel, bool json_mode) {
    if (ms == NULL || channel == NULL) return;

    size_t count = 0;
    ChannelMessage** msgs = channel_receive(ms, channel, ONESHOT_WAIT_SUBSCRIBER, 20, &count);
    if (msgs == NULL) return;

    for (size_t i = 0; i < count; i++) {
        char* line = goal_progress_format(msgs[i]->content);
        if (line != NULL) {
            emit_line(json_mode, "goal_progress", line);
            free(line);
        }
    }

    channel_message_free_list(msgs, count);
}

/* "N goal(s) still running in the background (...). Run scaffold here and
 * /goals to check on them." — reused verbatim from the REPL quit warning
 * (Task 5.3) since reattaching to a goal works the same way whether the
 * process exited via /quit or via Ctrl+C here. */
static void emit_interrupt_hint(TrackedGoal* tracked, size_t tracked_count,
                                 goal_store_t* store, bool json_mode) {
    const char* names[ONESHOT_WAIT_MAX_TRACKED];
    size_t live_count = 0;

    for (size_t i = 0; i < tracked_count && live_count < ONESHOT_WAIT_MAX_TRACKED; i++) {
        Goal* g = goal_store_get(store, tracked[i].id);
        if (g == NULL) continue;
        bool terminal = (g->status == GOAL_STATUS_COMPLETED || g->status == GOAL_STATUS_FAILED);
        if (!terminal) {
            names[live_count++] = tracked[i].name;
        }
        goal_free(g);
    }

    if (live_count == 0) return;

    char* warning = goal_progress_quit_warning(names, live_count);
    if (warning == NULL) return;
    emit_line(json_mode, "interrupted", warning);
    free(warning);
}

static void emit_failure(const Goal* g, bool json_mode) {
    const char* reason = (g->summary != NULL && g->summary[0] != '\0')
                              ? g->summary
                              : "no further details available";
    char* log_path = orchestrator_supervisor_log_path(g->id);

    char line[512];
    if (log_path != NULL) {
        snprintf(line, sizeof(line), "Goal %s failed: %s (log: %s)", g->name, reason, log_path);
    } else {
        snprintf(line, sizeof(line), "Goal %s failed: %s", g->name, reason);
    }
    emit_line(json_mode, "goal_failed", line);
    free(log_path);
}

static void emit_completed(const Goal* g, bool json_mode) {
    char line[256];
    snprintf(line, sizeof(line), "Goal %s completed.", g->name);
    emit_line(json_mode, "goal_completed", line);
}

int oneshot_wait_for_goals(AgentSession* session, const char* project,
                            time_t started_after, int poll_ms) {
    if (session == NULL || session->services == NULL || project == NULL) {
        return 0;
    }

    goal_store_t* store = services_get_goal_store(session->services);
    if (store == NULL) {
        return 0;
    }

    if (poll_ms <= 0) {
        poll_ms = ONESHOT_WAIT_DEFAULT_POLL_MS;
    }

    /* Scope to goals created during this run — never wait on pre-existing
     * goals from an earlier invocation. */
    size_t total = 0;
    Goal** all = goal_store_list_by_project(store, project, &total);

    TrackedGoal tracked[ONESHOT_WAIT_MAX_TRACKED];
    size_t tracked_count = 0;
    for (size_t i = 0; i < total && tracked_count < ONESHOT_WAIT_MAX_TRACKED; i++) {
        if (all[i]->created_at >= started_after) {
            snprintf(tracked[tracked_count].id, sizeof(tracked[tracked_count].id),
                     "%s", all[i]->id);
            snprintf(tracked[tracked_count].name, sizeof(tracked[tracked_count].name),
                     "%s", all[i]->name);
            tracked_count++;
        }
    }
    goal_free_list(all, total);

    if (tracked_count == 0) {
        return 0;
    }

    bool json_mode = session->session_data.config.json_output_mode;
    message_store_t* ms = services_get_message_store(session->services);
    char* channel = goal_progress_channel_name(project);
    if (ms != NULL && channel != NULL) {
        goal_progress_subscribe(ms, project, ONESHOT_WAIT_SUBSCRIBER);
    }

    if (interrupt_init() != 0) {
        fprintf(stderr, "Warning: failed to install interrupt handler while waiting for goals\n");
    }

    bool interrupted = false;

    while (!interrupted) {
        if (interrupt_pending()) {
            interrupted = true;
            break;
        }

        drain_progress(ms, channel, json_mode);

        bool all_terminal = true;
        time_t now = time(NULL);
        for (size_t i = 0; i < tracked_count; i++) {
            Goal* g = goal_store_get(store, tracked[i].id);
            if (g == NULL) continue;

            if (g->status != GOAL_STATUS_COMPLETED && g->status != GOAL_STATUS_FAILED) {
                all_terminal = false;
                /* Bounded by the goal's own lifecycle: the supervisor's own
                 * idle re-prompt / give-up budget (Task 5.2) eventually ends
                 * the goal even if respawn keeps finding a dead process. */
                if ((g->status == GOAL_STATUS_PLANNING || g->status == GOAL_STATUS_ACTIVE) &&
                    !orchestrator_supervisor_alive(g, now)) {
                    orchestrator_respawn_dead(store, project);
                }
            }
            goal_free(g);
        }

        if (all_terminal) break;

        if (interrupt_pending()) {
            interrupted = true;
            break;
        }

        usleep(poll_ms * 1000);
    }

    /* Final drain so progress events published right before the terminal
     * transition are not lost. */
    drain_progress(ms, channel, json_mode);

    int exit_code;
    if (interrupted) {
        emit_interrupt_hint(tracked, tracked_count, store, json_mode);
        exit_code = 130;
    } else {
        bool had_failure = false;
        for (size_t i = 0; i < tracked_count; i++) {
            Goal* g = goal_store_get(store, tracked[i].id);
            if (g == NULL) continue;
            if (g->status == GOAL_STATUS_FAILED) {
                had_failure = true;
                emit_failure(g, json_mode);
            } else if (g->status == GOAL_STATUS_COMPLETED) {
                emit_completed(g, json_mode);
            }
            goal_free(g);
        }
        exit_code = had_failure ? 1 : 0;
    }

    interrupt_cleanup();
    free(channel);
    return exit_code;
}
