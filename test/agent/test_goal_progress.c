#include "unity.h"
#include "orchestrator/goal_progress.h"
#include "orchestrator/supervisor.h"
#include "ipc/message_store.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "services/services.h"
#include "util/app_home.h"
#include "util/ansi_codes.h"
#include "../test_fs_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_test_db_path[256];
static char g_goal_db_path[256];
static message_store_t *g_store = NULL;
static goal_store_t *g_gs = NULL;
static action_store_t *g_as = NULL;
static Services *g_svc = NULL;

void setUp(void) {
    app_home_init(NULL);
    snprintf(g_test_db_path, sizeof(g_test_db_path), "/tmp/test_goal_progress_%d.db", getpid());
    snprintf(g_goal_db_path, sizeof(g_goal_db_path), "/tmp/test_goal_progress_goals_%d.db", getpid());
    unlink_sqlite_db(g_test_db_path);
    unlink_sqlite_db(g_goal_db_path);
    g_store = message_store_create(g_test_db_path);
    g_gs = goal_store_create(g_goal_db_path);
    g_as = action_store_create(g_goal_db_path);
    g_svc = services_create_empty();
    g_svc->message_store = g_store;
    g_svc->goal_store = g_gs;
    g_svc->action_store = g_as;
}

void tearDown(void) {
    if (g_svc != NULL) {
        /* services_destroy() frees message_store/goal_store/action_store too. */
        services_destroy(g_svc);
        g_svc = NULL;
        g_store = NULL;
        g_gs = NULL;
        g_as = NULL;
    }
    unlink_sqlite_db(g_test_db_path);
    unlink_sqlite_db(g_goal_db_path);
    app_home_cleanup();
}

/* ========================================================================
 * Publish / subscribe scoping
 * ======================================================================== */

void test_publish_then_subscriber_receives(void) {
    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-a", "repl"));

    int rc = goal_progress_publish(g_store, "proj-a", "action_finished",
                                    "todo-web-app", "Write tests/conftest.py (3/7)");
    TEST_ASSERT_EQUAL(0, rc);

    size_t count = 0;
    ChannelMessage **msgs = channel_receive(g_store, "goals:proj-a", "repl", 10, &count);
    TEST_ASSERT_NOT_NULL(msgs);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(strstr(msgs[0]->content, "action_finished"));
    TEST_ASSERT_NOT_NULL(strstr(msgs[0]->content, "todo-web-app"));
    channel_message_free_list(msgs, count);
}

void test_other_project_not_received(void) {
    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-a", "repl"));

    /* Event published for a different project must not show up for a
     * subscriber scoped to proj-a. */
    int rc = goal_progress_publish(g_store, "proj-b", "action_finished",
                                    "other-goal", "Some other action (1/1)");
    TEST_ASSERT_EQUAL(0, rc);

    size_t count = 0;
    ChannelMessage **msgs = channel_receive(g_store, "goals:proj-a", "repl", 10, &count);
    TEST_ASSERT_NULL(msgs);
    TEST_ASSERT_EQUAL(0, count);
}

void test_publish_requires_store_and_event(void) {
    TEST_ASSERT_EQUAL(-1, goal_progress_publish(NULL, "proj-a", "action_finished", "goal", "detail"));
    TEST_ASSERT_EQUAL(-1, goal_progress_publish(g_store, "proj-a", NULL, "goal", "detail"));
    TEST_ASSERT_EQUAL(-1, goal_progress_publish(g_store, "proj-a", "action_finished", NULL, "detail"));
}

/* ========================================================================
 * Formatting
 * ======================================================================== */

void test_format_action_finished(void) {
    char *line = goal_progress_format(
        "{\"event\":\"action_finished\",\"goal_name\":\"todo-web-app\","
        "\"detail\":\"Write tests/conftest.py (3/7)\"}");
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING(
        "\xE2\x97\x86 todo-web-app: \xE2\x9C\x93 Write tests/conftest.py (3/7)", line);
    free(line);
}

void test_format_action_failed(void) {
    char *line = goal_progress_format(
        "{\"event\":\"action_failed\",\"goal_name\":\"todo-web-app\","
        "\"detail\":\"Run migration (2/7)\"}");
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING(
        "\xE2\x97\x86 todo-web-app: \xE2\x9C\x97 Run migration (2/7)", line);
    free(line);
}

void test_format_rejects_malformed_json(void) {
    TEST_ASSERT_NULL(goal_progress_format("not json"));
    TEST_ASSERT_NULL(goal_progress_format(NULL));
}

void test_format_rejects_missing_goal_name(void) {
    TEST_ASSERT_NULL(goal_progress_format("{\"event\":\"action_finished\",\"detail\":\"x\"}"));
}

void test_format_action_skipped(void) {
    char *line = goal_progress_format(
        "{\"event\":\"action_skipped\",\"goal_name\":\"todo-web-app\","
        "\"detail\":\"Skip migration (2/7)\"}");
    TEST_ASSERT_NOT_NULL(line);
    TEST_ASSERT_EQUAL_STRING(
        "\xE2\x97\x86 todo-web-app: " TERM_SYM_INFO " Skip migration (2/7)", line);
    free(line);
}

/* ========================================================================
 * Quit warning
 * ======================================================================== */

void test_quit_warning_text(void) {
    const char *names[] = { "todo-web-app", "api" };
    char *msg = goal_progress_quit_warning(names, 2);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL_STRING(
        "2 goals still running in the background (todo-web-app, api). "
        "Run scaffold here and /goals to check on them.", msg);
    free(msg);
}

void test_quit_warning_singular(void) {
    const char *names[] = { "todo-web-app" };
    char *msg = goal_progress_quit_warning(names, 1);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_EQUAL_STRING(
        "1 goal still running in the background (todo-web-app). "
        "Run scaffold here and /goals to check on them.", msg);
    free(msg);
}

void test_quit_warning_empty(void) {
    TEST_ASSERT_NULL(goal_progress_quit_warning(NULL, 0));
    const char *names[] = { "todo-web-app" };
    TEST_ASSERT_NULL(goal_progress_quit_warning(names, 0));
}

/* ========================================================================
 * Channel retention (FIX 2: bounded growth)
 * ======================================================================== */

void test_progress_channel_is_non_persistent(void) {
    goal_progress_publish(g_store, "proj-c", "action_finished", "goal", "detail");

    Channel *ch = channel_get(g_store, "goals:proj-c");
    TEST_ASSERT_NOT_NULL(ch);
    TEST_ASSERT_EQUAL(0, ch->is_persistent);
    channel_free(ch);
}

void test_progress_channel_cleanup_removes_old_messages(void) {
    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-d", "repl"));
    goal_progress_publish(g_store, "proj-d", "action_finished", "goal", "detail");

    /* Short, bounded wait so the published row's created_at is strictly
     * before the cleanup cutoff below (millisecond-resolution timestamps). */
    usleep(20000);

    int rc = goal_progress_cleanup(g_store, 0);
    TEST_ASSERT_TRUE(rc >= 0);

    size_t count = 0;
    ChannelMessage **msgs = channel_receive(g_store, "goals:proj-d", "repl", 10, &count);
    TEST_ASSERT_NULL(msgs);
    TEST_ASSERT_EQUAL(0, count);
}

/* Key regression guard: goal_progress_cleanup() must only ever reach the
 * "goals:" namespace. A non-progress, non-persistent channel (as
 * messaging_tool.c creates for agent-to-agent coordination) sharing the
 * same message store must be untouched, even though the OLD unscoped
 * message_cleanup_channel_messages() would have deleted it too. */
void test_cleanup_does_not_touch_other_channels(void) {
    TEST_ASSERT_EQUAL(0, channel_create(g_store, "agents:chat", "Agent coordination",
                                         "worker-1", 0));
    TEST_ASSERT_EQUAL(0, channel_subscribe(g_store, "agents:chat", "worker-2"));
    char other_msg_id[40];
    TEST_ASSERT_EQUAL(0, channel_publish(g_store, "agents:chat", "worker-1",
                                          "unrelated coordination message", other_msg_id));

    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-h", "repl"));
    goal_progress_publish(g_store, "proj-h", "action_finished", "goal", "detail");

    usleep(20000);

    int rc = goal_progress_cleanup(g_store, 0);
    TEST_ASSERT_TRUE(rc >= 0);

    /* Progress message is gone. */
    size_t progress_count = 0;
    ChannelMessage **progress_msgs = channel_receive(g_store, "goals:proj-h", "repl", 10,
                                                      &progress_count);
    TEST_ASSERT_NULL(progress_msgs);
    TEST_ASSERT_EQUAL(0, progress_count);

    /* Unrelated agent channel message survives. */
    size_t other_count = 0;
    ChannelMessage **other_msgs = channel_receive(g_store, "agents:chat", "worker-2", 10,
                                                   &other_count);
    TEST_ASSERT_NOT_NULL(other_msgs);
    TEST_ASSERT_EQUAL(1, other_count);
    TEST_ASSERT_EQUAL_STRING("unrelated coordination message", other_msgs[0]->content);
    channel_message_free_list(other_msgs, other_count);
}

void test_ensure_channel_self_heals_legacy_persistent_row(void) {
    /* Simulate a channel created by a pre-fix build, which used
     * is_persistent=1. channel_create() below is then a silent no-op (the
     * row already exists), so without the self-heal in ensure_channel_exists
     * the flag would never flip and cleanup would stay a permanent no-op
     * for this channel. */
    TEST_ASSERT_EQUAL(0, channel_create(g_store, "goals:proj-i", "Goal progress events",
                                         "system", 1));

    Channel *before = channel_get(g_store, "goals:proj-i");
    TEST_ASSERT_NOT_NULL(before);
    TEST_ASSERT_EQUAL(1, before->is_persistent);
    channel_free(before);

    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-i", "repl"));

    Channel *after = channel_get(g_store, "goals:proj-i");
    TEST_ASSERT_NOT_NULL(after);
    TEST_ASSERT_EQUAL(0, after->is_persistent);
    channel_free(after);
}

/* ========================================================================
 * Snapshot seeding (FIX 1: no replay on supervisor restart)
 * ======================================================================== */

static void make_completed_action(char *out_goal_id, char *out_action_id) {
    goal_store_insert(g_gs, "Test Goal", "desc", "{}", "q", out_goal_id);
    action_store_insert(g_as, out_goal_id, NULL, "Write tests/conftest.py",
                        NULL, NULL, false, NULL, out_action_id);
    action_store_update_status(g_as, out_action_id, ACTION_STATUS_COMPLETED, "ok");
}

void test_seed_snapshot_prevents_replay_on_restart(void) {
    char goal_id[40], action_id[40];
    make_completed_action(goal_id, action_id);

    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-e", "repl"));

    /* Simulate a supervisor (re)starting on a goal that already has a
     * COMPLETED action from a previous process lifetime: seed first, then
     * run the same diff a real poll would run. */
    ProgressSnapshotEntry snapshot[SUPERVISOR_PROGRESS_SNAPSHOT_MAX];
    size_t snapshot_count = 0;
    supervisor_seed_action_snapshot(g_svc, goal_id, snapshot, &snapshot_count);
    TEST_ASSERT_EQUAL(1, snapshot_count);

    supervisor_publish_action_events(g_svc, goal_id, "proj-e", "Test Goal",
                                      snapshot, &snapshot_count);

    size_t count = 0;
    ChannelMessage **msgs = channel_receive(g_store, "goals:proj-e", "repl", 10, &count);
    TEST_ASSERT_NULL(msgs);
    TEST_ASSERT_EQUAL(0, count);
}

void test_unseeded_snapshot_still_detects_genuine_transition(void) {
    char goal_id[40], action_id[40];
    make_completed_action(goal_id, action_id);

    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-f", "repl"));

    /* No seeding: a fresh snapshot starting at count 0 (a genuinely new
     * supervisor_run() with no prior actions) must still report real,
     * first-time-seen completions. */
    ProgressSnapshotEntry snapshot[SUPERVISOR_PROGRESS_SNAPSHOT_MAX];
    size_t snapshot_count = 0;

    supervisor_publish_action_events(g_svc, goal_id, "proj-f", "Test Goal",
                                      snapshot, &snapshot_count);

    size_t count = 0;
    ChannelMessage **msgs = channel_receive(g_store, "goals:proj-f", "repl", 10, &count);
    TEST_ASSERT_NOT_NULL(msgs);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(strstr(msgs[0]->content, "action_finished"));
    channel_message_free_list(msgs, count);
}

void test_skipped_action_publishes_event(void) {
    char goal_id[40], action_id[40];
    goal_store_insert(g_gs, "Test Goal", "desc", "{}", "q", goal_id);
    action_store_insert(g_as, goal_id, NULL, "Optional cleanup step",
                        NULL, NULL, false, NULL, action_id);
    action_store_update_status(g_as, action_id, ACTION_STATUS_SKIPPED, NULL);

    TEST_ASSERT_EQUAL(0, goal_progress_subscribe(g_store, "proj-g", "repl"));

    ProgressSnapshotEntry snapshot[SUPERVISOR_PROGRESS_SNAPSHOT_MAX];
    size_t snapshot_count = 0;
    supervisor_publish_action_events(g_svc, goal_id, "proj-g", "Test Goal",
                                      snapshot, &snapshot_count);

    size_t count = 0;
    ChannelMessage **msgs = channel_receive(g_store, "goals:proj-g", "repl", 10, &count);
    TEST_ASSERT_NOT_NULL(msgs);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_NOT_NULL(strstr(msgs[0]->content, "action_skipped"));
    channel_message_free_list(msgs, count);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_then_subscriber_receives);
    RUN_TEST(test_other_project_not_received);
    RUN_TEST(test_publish_requires_store_and_event);
    RUN_TEST(test_format_action_finished);
    RUN_TEST(test_format_action_failed);
    RUN_TEST(test_format_action_skipped);
    RUN_TEST(test_format_rejects_malformed_json);
    RUN_TEST(test_format_rejects_missing_goal_name);
    RUN_TEST(test_quit_warning_text);
    RUN_TEST(test_quit_warning_singular);
    RUN_TEST(test_quit_warning_empty);
    RUN_TEST(test_progress_channel_is_non_persistent);
    RUN_TEST(test_progress_channel_cleanup_removes_old_messages);
    RUN_TEST(test_cleanup_does_not_touch_other_channels);
    RUN_TEST(test_ensure_channel_self_heals_legacy_persistent_row);
    RUN_TEST(test_seed_snapshot_prevents_replay_on_restart);
    RUN_TEST(test_unseeded_snapshot_still_detects_genuine_transition);
    RUN_TEST(test_skipped_action_publishes_event);
    return UNITY_END();
}
