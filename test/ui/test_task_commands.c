#include "../../test/unity/unity.h"
#include "ui/task_commands.h"
#include "agent/session.h"
#include "services/services.h"
#include "db/task_store.h"
#include "util/app_home.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../test_fs_utils.h"

#define TEST_DB_PATH "/tmp/test_task_commands.db"

static AgentSession g_session;
static Services *g_services = NULL;

static void redirect_stdout(void) {
    freopen("/dev/null", "w", stdout);
}

static void restore_stdout(void) {
    freopen("/dev/tty", "w", stdout);
}

void setUp(void) {
    app_home_init(NULL);
    unlink_sqlite_db(TEST_DB_PATH);

    g_services = services_create_empty();
    g_services->task_store = task_store_create(TEST_DB_PATH);

    memset(&g_session, 0, sizeof(g_session));
    g_session.services = g_services;

    redirect_stdout();
}

void tearDown(void) {
    restore_stdout();
    if (g_services) {
        services_destroy(g_services);
        g_services = NULL;
    }
    unlink_sqlite_db(TEST_DB_PATH);
    app_home_cleanup();
}

static task_store_t *store(void) {
    return services_get_task_store(g_services);
}

void test_task_list_empty(void) {
    int result = process_task_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_list_explicit(void) {
    int result = process_task_command("list", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_list_with_tasks(void) {
    char id[40];
    task_store_create_task(store(), "global", "Buy groceries", TASK_PRIORITY_HIGH, NULL, id);
    task_store_create_task(store(), "global", "Write tests", TASK_PRIORITY_MEDIUM, NULL, id);

    int result = process_task_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_ready(void) {
    char id1[40], id2[40];
    task_store_create_task(store(), "global", "First", TASK_PRIORITY_HIGH, NULL, id1);
    task_store_create_task(store(), "global", "Second", TASK_PRIORITY_LOW, NULL, id2);
    task_store_add_dependency(store(), id2, id1);

    int result = process_task_command("ready", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_show_by_prefix(void) {
    char id[40];
    task_store_create_task(store(), "global", "Important task", TASK_PRIORITY_HIGH, NULL, id);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "show %.8s", id);
    int result = process_task_command(cmd, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_show_not_found(void) {
    int result = process_task_command("show deadbeef", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_show_no_id(void) {
    int result = process_task_command("show ", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_help(void) {
    int result = process_task_command("help", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_unknown_subcommand(void) {
    int result = process_task_command("bogus", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_null_args(void) {
    int result = process_task_command(NULL, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_task_show_with_dependencies(void) {
    char id1[40], id2[40];
    task_store_create_task(store(), "global", "Blocker", TASK_PRIORITY_HIGH, NULL, id1);
    task_store_create_task(store(), "global", "Blocked", TASK_PRIORITY_MEDIUM, NULL, id2);
    task_store_add_dependency(store(), id2, id1);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "show %.8s", id2);
    int result = process_task_command(cmd, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_task_list_empty);
    RUN_TEST(test_task_list_explicit);
    RUN_TEST(test_task_list_with_tasks);
    RUN_TEST(test_task_ready);
    RUN_TEST(test_task_show_by_prefix);
    RUN_TEST(test_task_show_not_found);
    RUN_TEST(test_task_show_no_id);
    RUN_TEST(test_task_help);
    RUN_TEST(test_task_unknown_subcommand);
    RUN_TEST(test_task_null_args);
    RUN_TEST(test_task_show_with_dependencies);

    return UNITY_END();
}
