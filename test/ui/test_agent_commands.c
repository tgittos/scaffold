#include "../../test/unity/unity.h"
#include "ui/agent_commands.h"
#include "agent/session.h"
#include "tools/subagent_tool.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static AgentSession g_session;

static void redirect_stdout(void) {
    freopen("/dev/null", "w", stdout);
}

static void restore_stdout(void) {
    freopen("/dev/tty", "w", stdout);
}

void setUp(void) {
    memset(&g_session, 0, sizeof(g_session));
    subagent_manager_init_with_config(&g_session.subagent_manager,
                                      SUBAGENT_MAX_DEFAULT,
                                      SUBAGENT_TIMEOUT_DEFAULT);
    redirect_stdout();
}

void tearDown(void) {
    restore_stdout();
    subagent_manager_cleanup(&g_session.subagent_manager);
}

void test_agents_list_empty(void) {
    int result = process_agent_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_agents_list_explicit(void) {
    int result = process_agent_command("list", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_agents_help(void) {
    int result = process_agent_command("help", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_agents_unknown_subcommand(void) {
    int result = process_agent_command("bogus", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_agents_null_args(void) {
    int result = process_agent_command(NULL, &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_agents_show_not_found(void) {
    int result = process_agent_command("show deadbeef", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

void test_agents_show_no_id(void) {
    int result = process_agent_command("show ", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

/* Add a fake subagent entry directly to the manager for display testing */
static void add_fake_subagent(SubagentManager *mgr, const char *id,
                               const char *task, SubagentStatus status) {
    Subagent sub;
    memset(&sub, 0, sizeof(sub));
    snprintf(sub.id, sizeof(sub.id), "%s", id);
    sub.task = strdup(task);
    sub.status = status;
    sub.start_time = time(NULL) - 30;
    sub.stdout_pipe[0] = -1;
    sub.stdout_pipe[1] = -1;
    sub.approval_channel.request_fd = -1;
    sub.approval_channel.response_fd = -1;
    SubagentArray_push(&mgr->subagents, sub);
}

void test_agents_list_with_subagents(void) {
    add_fake_subagent(&g_session.subagent_manager, "abcdef1234567890",
                      "Research something", SUBAGENT_STATUS_COMPLETED);
    add_fake_subagent(&g_session.subagent_manager, "1234567890abcdef",
                      "Write code", SUBAGENT_STATUS_RUNNING);

    int result = process_agent_command("", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_size_t(2, g_session.subagent_manager.subagents.count);
}

void test_agents_show_by_prefix(void) {
    add_fake_subagent(&g_session.subagent_manager, "abcdef1234567890",
                      "Research something", SUBAGENT_STATUS_COMPLETED);

    int result = process_agent_command("show abcdef12", &g_session);
    TEST_ASSERT_EQUAL_INT(0, result);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_agents_list_empty);
    RUN_TEST(test_agents_list_explicit);
    RUN_TEST(test_agents_help);
    RUN_TEST(test_agents_unknown_subcommand);
    RUN_TEST(test_agents_null_args);
    RUN_TEST(test_agents_show_not_found);
    RUN_TEST(test_agents_show_no_id);
    RUN_TEST(test_agents_list_with_subagents);
    RUN_TEST(test_agents_show_by_prefix);

    return UNITY_END();
}
