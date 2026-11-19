#include "unity.h"
#include "orchestrator/goal_progress.h"
#include "ipc/message_store.h"
#include "util/app_home.h"
#include "../test_fs_utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char g_test_db_path[256];
static message_store_t *g_store = NULL;

void setUp(void) {
    app_home_init(NULL);
    snprintf(g_test_db_path, sizeof(g_test_db_path), "/tmp/test_goal_progress_%d.db", getpid());
    unlink_sqlite_db(g_test_db_path);
    g_store = message_store_create(g_test_db_path);
}

void tearDown(void) {
    if (g_store != NULL) {
        message_store_destroy(g_store);
        g_store = NULL;
    }
    unlink_sqlite_db(g_test_db_path);
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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_publish_then_subscriber_receives);
    RUN_TEST(test_other_project_not_received);
    RUN_TEST(test_publish_requires_store_and_event);
    RUN_TEST(test_format_action_finished);
    RUN_TEST(test_format_action_failed);
    RUN_TEST(test_format_rejects_malformed_json);
    RUN_TEST(test_format_rejects_missing_goal_name);
    RUN_TEST(test_quit_warning_text);
    RUN_TEST(test_quit_warning_singular);
    RUN_TEST(test_quit_warning_empty);
    return UNITY_END();
}
