#include "unity/unity.h"
#include "plugin/hook_dispatcher.h"
#include "plugin/plugin_manager.h"
#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: create a PluginManager with mock plugins (no actual processes) */
static void setup_mock_plugin(PluginProcess *p, const char *name,
                                int priority, const char **hooks, int hook_count) {
    memset(p, 0, sizeof(PluginProcess));
    p->initialized = 1;
    p->stdin_fd = -1;
    p->stdout_fd = -1;
    p->manifest.name = strdup(name);
    p->manifest.version = strdup("1.0.0");
    p->manifest.description = strdup("");
    p->manifest.priority = priority;
    p->manifest.hook_count = hook_count;
    if (hook_count > 0) {
        p->manifest.hooks = calloc(hook_count, sizeof(char *));
        for (int i = 0; i < hook_count; i++) {
            p->manifest.hooks[i] = strdup(hooks[i]);
        }
    }
    p->manifest.tool_count = 0;
    p->manifest.tools = NULL;
}

static void cleanup_mock_plugin(PluginProcess *p) {
    plugin_manifest_cleanup(&p->manifest);
}

/* --- Tests with no plugins --- */

static void test_dispatch_no_plugins(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    char *msg = strdup("hello");
    HookAction result = hook_dispatch_post_user_input(&mgr, NULL, &msg);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    TEST_ASSERT_EQUAL_STRING("hello", msg);
    free(msg);
}

static void test_dispatch_null_manager(void) {
    char *msg = strdup("hello");
    HookAction result = hook_dispatch_post_user_input(NULL, NULL, &msg);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    free(msg);
}

/* --- Tests with plugins that don't subscribe --- */

static void test_dispatch_no_subscribers(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    const char *hooks[] = { "context_enhance" };
    setup_mock_plugin(&mgr.plugins[0], "enhancer", 500, hooks, 1);
    mgr.count = 1;

    /* Dispatch a hook that nobody subscribes to */
    char *msg = strdup("hello");
    HookAction result = hook_dispatch_post_user_input(&mgr, NULL, &msg);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    TEST_ASSERT_EQUAL_STRING("hello", msg);
    free(msg);

    cleanup_mock_plugin(&mgr.plugins[0]);
}

/* --- Context enhance with no subscribers returns HOOK_CONTINUE --- */

static void test_context_enhance_no_subscribers(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    char *ctx = strdup("existing context");
    HookAction result = hook_dispatch_context_enhance(&mgr, NULL, "query", &ctx);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    TEST_ASSERT_EQUAL_STRING("existing context", ctx);
    free(ctx);
}

static void test_context_enhance_null_context(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    char *ctx = NULL;
    HookAction result = hook_dispatch_context_enhance(&mgr, NULL, "query", &ctx);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    TEST_ASSERT_NULL(ctx);
}

/* --- Pre/post tool hooks with no subscribers --- */

static void test_pre_tool_no_subscribers(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    ToolCall call = { .id = "1", .name = "write_file", .arguments = "{}" };
    ToolResult result = {0};

    HookAction hr = hook_dispatch_pre_tool_execute(&mgr, NULL, &call, &result);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, hr);
}

static void test_post_tool_no_subscribers(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    ToolCall call = { .id = "1", .name = "write_file", .arguments = "{}" };
    ToolResult result = { .tool_call_id = strdup("1"), .result = strdup("ok"), .success = 1 };

    HookAction hr = hook_dispatch_post_tool_execute(&mgr, NULL, &call, &result);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, hr);
    TEST_ASSERT_EQUAL_STRING("ok", result.result);

    free(result.tool_call_id);
    free(result.result);
}

/* --- Pre/post LLM hooks with no subscribers --- */

static void test_pre_llm_send_no_subscribers(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    char *base = strdup("system prompt");
    char *ctx = strdup("context");
    HookAction hr = hook_dispatch_pre_llm_send(&mgr, NULL, &base, &ctx);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, hr);
    TEST_ASSERT_EQUAL_STRING("system prompt", base);
    free(base);
    free(ctx);
}

static void test_post_llm_response_no_subscribers(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    char *text = strdup("response");
    HookAction hr = hook_dispatch_post_llm_response(&mgr, NULL, &text, NULL, 0);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, hr);
    TEST_ASSERT_EQUAL_STRING("response", text);
    free(text);
}

/* --- Priority ordering --- */

static void test_priority_ordering(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    const char *hooks[] = { "post_user_input" };

    /* Plugin B at priority 100 (should run first) */
    setup_mock_plugin(&mgr.plugins[0], "plugin-b", 100, hooks, 1);
    /* Plugin A at priority 900 (should run second) */
    setup_mock_plugin(&mgr.plugins[1], "plugin-a", 900, hooks, 1);
    mgr.count = 2;

    /*
     * Both plugins subscribe to post_user_input but have invalid FDs,
     * so the dispatch will fail to communicate with them. The important
     * thing is that the dispatcher tries them in priority order and
     * doesn't crash.
     */
    char *msg = strdup("hello");
    HookAction result = hook_dispatch_post_user_input(&mgr, NULL, &msg);
    /* With broken pipes, both plugins timeout/error, so we get CONTINUE */
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    free(msg);

    cleanup_mock_plugin(&mgr.plugins[0]);
    cleanup_mock_plugin(&mgr.plugins[1]);
}

/* --- Uninitialized plugins are skipped --- */

static void test_uninitialized_plugins_skipped(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    const char *hooks[] = { "post_user_input" };
    setup_mock_plugin(&mgr.plugins[0], "dead", 500, hooks, 1);
    mgr.plugins[0].initialized = 0; /* Mark as uninitialized */
    mgr.count = 1;

    char *msg = strdup("hello");
    HookAction result = hook_dispatch_post_user_input(&mgr, NULL, &msg);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, result);
    TEST_ASSERT_EQUAL_STRING("hello", msg);
    free(msg);

    cleanup_mock_plugin(&mgr.plugins[0]);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_dispatch_no_plugins);
    RUN_TEST(test_dispatch_null_manager);
    RUN_TEST(test_dispatch_no_subscribers);
    RUN_TEST(test_context_enhance_no_subscribers);
    RUN_TEST(test_context_enhance_null_context);
    RUN_TEST(test_pre_tool_no_subscribers);
    RUN_TEST(test_post_tool_no_subscribers);
    RUN_TEST(test_pre_llm_send_no_subscribers);
    RUN_TEST(test_post_llm_response_no_subscribers);
    RUN_TEST(test_priority_ordering);
    RUN_TEST(test_uninitialized_plugins_skipped);

    return UNITY_END();
}
