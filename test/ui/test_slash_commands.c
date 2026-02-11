#include "../../test/unity/unity.h"
#include "ui/slash_commands.h"
#include <stdio.h>
#include <string.h>

/* Track which handler was called and with what args */
static int g_handler_called;
static char g_handler_args[256];
static AgentSession *g_handler_session;

static void reset_tracking(void) {
    g_handler_called = 0;
    g_handler_args[0] = '\0';
    g_handler_session = NULL;
}

static int mock_handler_a(const char *args, AgentSession *session) {
    g_handler_called = 1;
    snprintf(g_handler_args, sizeof(g_handler_args), "%s", args);
    g_handler_session = session;
    return 0;
}

static int mock_handler_b(const char *args, AgentSession *session) {
    g_handler_called = 2;
    snprintf(g_handler_args, sizeof(g_handler_args), "%s", args);
    g_handler_session = session;
    return 0;
}

static int mock_handler_returns_error(const char *args, AgentSession *session) {
    (void)args; (void)session;
    g_handler_called = 99;
    return -1;
}

void setUp(void) {
    slash_commands_cleanup();
    reset_tracking();
}

void tearDown(void) {
    slash_commands_cleanup();
}

void test_register_and_dispatch(void) {
    TEST_ASSERT_EQUAL_INT(0, slash_command_register("foo", "desc", mock_handler_a));
    TEST_ASSERT_EQUAL_INT(0, slash_command_dispatch("/foo", NULL));
    TEST_ASSERT_EQUAL_INT(1, g_handler_called);
    TEST_ASSERT_EQUAL_STRING("", g_handler_args);
}

void test_dispatch_with_args(void) {
    slash_command_register("bar", "desc", mock_handler_a);
    slash_command_dispatch("/bar hello world", NULL);
    TEST_ASSERT_EQUAL_INT(1, g_handler_called);
    TEST_ASSERT_EQUAL_STRING("hello world", g_handler_args);
}

void test_dispatch_strips_leading_spaces(void) {
    slash_command_register("cmd", "desc", mock_handler_a);
    slash_command_dispatch("/cmd   spaced", NULL);
    TEST_ASSERT_EQUAL_STRING("spaced", g_handler_args);
}

void test_dispatch_unknown_command(void) {
    slash_command_register("known", "desc", mock_handler_a);
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch("/unknown", NULL));
    TEST_ASSERT_EQUAL_INT(0, g_handler_called);
}

void test_dispatch_null_line(void) {
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch(NULL, NULL));
}

void test_dispatch_no_slash_prefix(void) {
    slash_command_register("cmd", "desc", mock_handler_a);
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch("cmd", NULL));
    TEST_ASSERT_EQUAL_INT(0, g_handler_called);
}

void test_dispatch_correct_handler(void) {
    slash_command_register("alpha", "desc", mock_handler_a);
    slash_command_register("beta", "desc", mock_handler_b);

    slash_command_dispatch("/beta arg", NULL);
    TEST_ASSERT_EQUAL_INT(2, g_handler_called);
    TEST_ASSERT_EQUAL_STRING("arg", g_handler_args);
}

void test_dispatch_prefix_not_matched(void) {
    slash_command_register("foo", "desc", mock_handler_a);
    /* "/foobar" should NOT match "/foo" — requires word boundary */
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch("/foobar", NULL));
    TEST_ASSERT_EQUAL_INT(0, g_handler_called);
}

void test_dispatch_passes_session(void) {
    /* Use an opaque non-NULL pointer — we only verify it's passed through */
    int sentinel;
    AgentSession *fake_session = (AgentSession *)&sentinel;

    slash_command_register("cmd", "desc", mock_handler_a);
    slash_command_dispatch("/cmd", fake_session);
    TEST_ASSERT_EQUAL_PTR(fake_session, g_handler_session);
}

void test_dispatch_returns_handler_result(void) {
    slash_command_register("fail", "desc", mock_handler_returns_error);
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch("/fail", NULL));
    TEST_ASSERT_EQUAL_INT(99, g_handler_called);
}

void test_register_overflow(void) {
    /* Fill to capacity */
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT(0, slash_command_register("x", "desc", mock_handler_a));
    }
    /* 17th should fail */
    TEST_ASSERT_EQUAL_INT(-1, slash_command_register("overflow", "desc", mock_handler_a));
}

void test_cleanup_resets(void) {
    slash_command_register("cmd", "desc", mock_handler_a);
    TEST_ASSERT_EQUAL_INT(0, slash_command_dispatch("/cmd", NULL));

    slash_commands_cleanup();
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch("/cmd", NULL));
}

void test_empty_registry(void) {
    TEST_ASSERT_EQUAL_INT(-1, slash_command_dispatch("/anything", NULL));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_register_and_dispatch);
    RUN_TEST(test_dispatch_with_args);
    RUN_TEST(test_dispatch_strips_leading_spaces);
    RUN_TEST(test_dispatch_unknown_command);
    RUN_TEST(test_dispatch_null_line);
    RUN_TEST(test_dispatch_no_slash_prefix);
    RUN_TEST(test_dispatch_correct_handler);
    RUN_TEST(test_dispatch_prefix_not_matched);
    RUN_TEST(test_dispatch_passes_session);
    RUN_TEST(test_dispatch_returns_handler_result);
    RUN_TEST(test_register_overflow);
    RUN_TEST(test_cleanup_resets);
    RUN_TEST(test_empty_registry);

    return UNITY_END();
}
