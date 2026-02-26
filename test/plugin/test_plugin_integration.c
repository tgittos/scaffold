#include "unity/unity.h"
#include "plugin/plugin_manager.h"
#include "plugin/plugin_protocol.h"
#include "plugin/hook_dispatcher.h"
#include "util/app_home.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

static char tmpdir[256];
static char plugins_path[300];
static char plugin_script_path[320];

/* Shell script plugin that handles JSON-RPC protocol.
 * Uses simple string matching to parse requests and respond. */
static const char *PLUGIN_SCRIPT =
    "#!/bin/sh\n"
    "while IFS= read -r line; do\n"
    "  case \"$line\" in\n"
    "    *'\"method\":\"initialize\"'*)\n"
    "      echo '{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"name\":\"test-ipc\",\"version\":\"1.0.0\",\"description\":\"Integration test plugin\",\"priority\":500,\"hooks\":[\"post_user_input\",\"pre_tool_execute\",\"post_tool_execute\"],\"tools\":[{\"name\":\"echo\",\"description\":\"Echo tool\",\"parameters\":[{\"name\":\"text\",\"type\":\"string\",\"description\":\"Text to echo\",\"required\":true}]}]}}'\n"
    "      ;;\n"
    "    *'\"method\":\"hook/post_user_input\"'*)\n"
    "      echo '{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"action\":\"continue\",\"message\":\"modified by plugin\"}}'\n"
    "      ;;\n"
    "    *'\"method\":\"hook/pre_tool_execute\"'*)\n"
    "      echo '{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{\"action\":\"continue\"}}'\n"
    "      ;;\n"
    "    *'\"method\":\"hook/post_tool_execute\"'*)\n"
    "      echo '{\"jsonrpc\":\"2.0\",\"id\":4,\"result\":{\"action\":\"continue\",\"result\":\"transformed result\"}}'\n"
    "      ;;\n"
    "    *'\"method\":\"tool/execute\"'*)\n"
    "      echo '{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":{\"success\":true,\"result\":\"echo: hello world\"}}'\n"
    "      ;;\n"
    "    *'\"method\":\"shutdown\"'*)\n"
    "      echo '{\"jsonrpc\":\"2.0\",\"id\":6,\"result\":{\"status\":\"ok\"}}'\n"
    "      exit 0\n"
    "      ;;\n"
    "  esac\n"
    "done\n";

static void setup_test_env(void) {
    strcpy(tmpdir, "/tmp/scaffold_plugin_int_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    int n = snprintf(plugins_path, sizeof(plugins_path), "%s/plugins", tmpdir);
    TEST_ASSERT_TRUE(n > 0 && (size_t)n < sizeof(plugins_path));
    mkdir(plugins_path, 0755);

    n = snprintf(plugin_script_path, sizeof(plugin_script_path), "%s/test-ipc", plugins_path);
    TEST_ASSERT_TRUE(n > 0 && (size_t)n < sizeof(plugin_script_path));
    FILE *f = fopen(plugin_script_path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(PLUGIN_SCRIPT, f);
    fclose(f);
    chmod(plugin_script_path, 0755);
}

static void cleanup_test_env(void) {
    unlink(plugin_script_path);
    rmdir(plugins_path);
    rmdir(tmpdir);
}

/* Full lifecycle: discover -> start -> handshake -> shutdown */
static void test_full_lifecycle(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);

    int discovered = plugin_manager_discover(&mgr);
    TEST_ASSERT_EQUAL(1, discovered);
    TEST_ASSERT_EQUAL(1, mgr.count);

    TEST_ASSERT_EQUAL(0, plugin_manager_start_all(&mgr, NULL));
    TEST_ASSERT_EQUAL(1, mgr.plugins[0].initialized);
    TEST_ASSERT_EQUAL_STRING("test-ipc", mgr.plugins[0].manifest.name);
    TEST_ASSERT_EQUAL_STRING("1.0.0", mgr.plugins[0].manifest.version);
    TEST_ASSERT_EQUAL(500, mgr.plugins[0].manifest.priority);
    TEST_ASSERT_EQUAL(3, mgr.plugins[0].manifest.hook_count);
    TEST_ASSERT_EQUAL(1, mgr.plugins[0].manifest.tool_count);
    TEST_ASSERT_EQUAL_STRING("echo", mgr.plugins[0].manifest.tools[0].name);

    plugin_manager_shutdown_all(&mgr);
    TEST_ASSERT_EQUAL(0, mgr.count);

    cleanup_test_env();
}

/* Hook dispatch: post_user_input modifies message */
static void test_hook_post_user_input(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_discover(&mgr);
    plugin_manager_start_all(&mgr, NULL);

    char *msg = strdup("original message");
    HookAction action = hook_dispatch_post_user_input(&mgr, NULL, &msg);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, action);
    TEST_ASSERT_EQUAL_STRING("modified by plugin", msg);
    free(msg);

    plugin_manager_shutdown_all(&mgr);
    cleanup_test_env();
}

/* Hook dispatch: post_tool_execute transforms result */
static void test_hook_post_tool_execute(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_discover(&mgr);
    plugin_manager_start_all(&mgr, NULL);

    ToolCall call = { .id = "1", .name = "write_file", .arguments = "{}" };
    ToolResult result = {
        .tool_call_id = strdup("1"),
        .result = strdup("original result"),
        .success = 1
    };

    HookAction action = hook_dispatch_post_tool_execute(&mgr, NULL, &call, &result);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, action);
    TEST_ASSERT_EQUAL_STRING("transformed result", result.result);

    free(result.tool_call_id);
    free(result.result);

    plugin_manager_shutdown_all(&mgr);
    cleanup_test_env();
}

/* Tool execution via plugin IPC */
static void test_tool_execution(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_discover(&mgr);
    plugin_manager_start_all(&mgr, NULL);

    ToolCall call = {
        .id = "call-1",
        .name = "plugin_test-ipc_echo",
        .arguments = "{\"text\":\"hello world\"}"
    };
    ToolResult result = {0};

    int rc = plugin_manager_execute_tool(&mgr, &call, &result);
    TEST_ASSERT_EQUAL(0, rc);
    TEST_ASSERT_EQUAL_STRING("call-1", result.tool_call_id);
    TEST_ASSERT_EQUAL_STRING("echo: hello world", result.result);
    TEST_ASSERT_EQUAL(1, result.success);

    free(result.tool_call_id);
    free(result.result);

    plugin_manager_shutdown_all(&mgr);
    cleanup_test_env();
}

/* Tool registration: plugin tools registered with correct prefix */
static void test_tool_registration(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_discover(&mgr);

    ToolRegistry registry = {0};
    TEST_ASSERT_EQUAL(0, plugin_manager_start_all(&mgr, &registry));

    /* Find the registered plugin tool */
    int found = 0;
    for (size_t i = 0; i < registry.functions.count; i++) {
        if (strcmp(registry.functions.data[i].name, "plugin_test-ipc_echo") == 0) {
            found = 1;
            TEST_ASSERT_EQUAL_STRING("Echo tool", registry.functions.data[i].description);
            TEST_ASSERT_EQUAL(0, registry.functions.data[i].thread_safe);
            TEST_ASSERT_EQUAL(1, registry.functions.data[i].parameter_count);
            break;
        }
    }
    TEST_ASSERT_TRUE(found);

    /* Cleanup registered tools */
    for (size_t i = 0; i < registry.functions.count; i++) {
        free(registry.functions.data[i].name);
        free(registry.functions.data[i].description);
        for (int j = 0; j < registry.functions.data[i].parameter_count; j++) {
            free(registry.functions.data[i].parameters[j].name);
            free(registry.functions.data[i].parameters[j].type);
            free(registry.functions.data[i].parameters[j].description);
        }
        free(registry.functions.data[i].parameters);
    }
    free(registry.functions.data);

    plugin_manager_shutdown_all(&mgr);
    cleanup_test_env();
}

/* Multiple sequential hook dispatches on same plugin */
static void test_multiple_hooks_sequentially(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_discover(&mgr);
    plugin_manager_start_all(&mgr, NULL);

    /* First: post_user_input */
    char *msg = strdup("first");
    hook_dispatch_post_user_input(&mgr, NULL, &msg);
    TEST_ASSERT_EQUAL_STRING("modified by plugin", msg);
    free(msg);

    /* Second: pre_tool_execute */
    ToolCall call = { .id = "1", .name = "test", .arguments = "{}" };
    ToolResult result = {0};
    HookAction action = hook_dispatch_pre_tool_execute(&mgr, NULL, &call, &result);
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, action);

    /* Third: post_tool_execute */
    result.tool_call_id = strdup("1");
    result.result = strdup("ok");
    result.success = 1;
    hook_dispatch_post_tool_execute(&mgr, NULL, &call, &result);
    TEST_ASSERT_EQUAL_STRING("transformed result", result.result);
    free(result.tool_call_id);
    free(result.result);

    /* Fourth: another post_user_input to verify plugin is still alive */
    msg = strdup("second");
    hook_dispatch_post_user_input(&mgr, NULL, &msg);
    TEST_ASSERT_EQUAL_STRING("modified by plugin", msg);
    free(msg);

    plugin_manager_shutdown_all(&mgr);
    cleanup_test_env();
}

/* Plugin process alive check after start */
static void test_alive_check(void) {
    setup_test_env();

    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_discover(&mgr);
    plugin_manager_start_all(&mgr, NULL);

    TEST_ASSERT_TRUE(plugin_check_alive(&mgr.plugins[0]));

    plugin_manager_shutdown_all(&mgr);
    cleanup_test_env();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_full_lifecycle);
    RUN_TEST(test_hook_post_user_input);
    RUN_TEST(test_hook_post_tool_execute);
    RUN_TEST(test_tool_execution);
    RUN_TEST(test_tool_registration);
    RUN_TEST(test_multiple_hooks_sequentially);
    RUN_TEST(test_alive_check);

    return UNITY_END();
}
