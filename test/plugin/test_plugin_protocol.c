#include "unity/unity.h"
#include "plugin/plugin_protocol.h"
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>

void setUp(void) {}
void tearDown(void) {}

/* --- Build tests --- */

static void test_build_initialize(void) {
    char *json = plugin_protocol_build_initialize(1);
    TEST_ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_EQUAL_STRING("2.0", cJSON_GetObjectItem(root, "jsonrpc")->valuestring);
    TEST_ASSERT_EQUAL_STRING("initialize", cJSON_GetObjectItem(root, "method")->valuestring);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(root, "id")->valueint);

    cJSON *params = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_EQUAL(1, cJSON_GetObjectItem(params, "protocol_version")->valueint);

    cJSON_Delete(root);
    free(json);
}

static void test_build_hook_event(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "message", "hello");

    char *json = plugin_protocol_build_hook_event("post_user_input", params);
    cJSON_Delete(params);
    TEST_ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_EQUAL_STRING("hook/post_user_input",
                              cJSON_GetObjectItem(root, "method")->valuestring);

    cJSON *p = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("hello", cJSON_GetObjectItem(p, "message")->valuestring);

    cJSON_Delete(root);
    free(json);
}

static void test_build_hook_event_null_params(void) {
    char *json = plugin_protocol_build_hook_event("context_enhance", NULL);
    TEST_ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *p = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(cJSON_IsObject(p));

    cJSON_Delete(root);
    free(json);
}

static void test_build_tool_execute(void) {
    char *json = plugin_protocol_build_tool_execute("git_log", "{\"count\":5}");
    TEST_ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_EQUAL_STRING("tool/execute",
                              cJSON_GetObjectItem(root, "method")->valuestring);

    cJSON *p = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_EQUAL_STRING("git_log", cJSON_GetObjectItem(p, "name")->valuestring);

    cJSON *args = cJSON_GetObjectItem(p, "arguments");
    TEST_ASSERT_NOT_NULL(args);
    TEST_ASSERT_EQUAL(5, cJSON_GetObjectItem(args, "count")->valueint);

    cJSON_Delete(root);
    free(json);
}

static void test_build_shutdown(void) {
    char *json = plugin_protocol_build_shutdown();
    TEST_ASSERT_NOT_NULL(json);

    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);

    TEST_ASSERT_EQUAL_STRING("shutdown", cJSON_GetObjectItem(root, "method")->valuestring);

    cJSON_Delete(root);
    free(json);
}

/* --- Parse tests --- */

static void test_parse_manifest_basic(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"name\":\"test-plugin\",\"version\":\"1.2.3\","
        "\"description\":\"A test plugin\","
        "\"hooks\":[\"post_user_input\",\"context_enhance\"],"
        "\"tools\":[],"
        "\"priority\":300"
        "},\"id\":1}";

    PluginManifest m;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_manifest(json, &m));

    TEST_ASSERT_EQUAL_STRING("test-plugin", m.name);
    TEST_ASSERT_EQUAL_STRING("1.2.3", m.version);
    TEST_ASSERT_EQUAL_STRING("A test plugin", m.description);
    TEST_ASSERT_EQUAL(300, m.priority);
    TEST_ASSERT_EQUAL(2, m.hook_count);
    TEST_ASSERT_EQUAL_STRING("post_user_input", m.hooks[0]);
    TEST_ASSERT_EQUAL_STRING("context_enhance", m.hooks[1]);
    TEST_ASSERT_EQUAL(0, m.tool_count);

    plugin_manifest_cleanup(&m);
}

static void test_parse_manifest_with_tools(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"name\":\"tool-plugin\",\"version\":\"0.1.0\","
        "\"description\":\"\","
        "\"hooks\":[],"
        "\"tools\":["
        "  {\"name\":\"my_tool\",\"description\":\"Does stuff\","
        "   \"parameters\":[{\"name\":\"arg1\",\"type\":\"string\",\"description\":\"First arg\",\"required\":true}]}"
        "],"
        "\"priority\":500"
        "},\"id\":1}";

    PluginManifest m;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_manifest(json, &m));

    TEST_ASSERT_EQUAL(1, m.tool_count);
    TEST_ASSERT_EQUAL_STRING("my_tool", m.tools[0].name);
    TEST_ASSERT_EQUAL_STRING("Does stuff", m.tools[0].description);
    TEST_ASSERT_EQUAL(1, m.tools[0].parameter_count);
    TEST_ASSERT_EQUAL_STRING("arg1", m.tools[0].parameters[0].name);
    TEST_ASSERT_EQUAL_STRING("string", m.tools[0].parameters[0].type);
    TEST_ASSERT_EQUAL(1, m.tools[0].parameters[0].required);

    plugin_manifest_cleanup(&m);
}

static void test_parse_manifest_defaults(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"name\":\"minimal\""
        "},\"id\":1}";

    PluginManifest m;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_manifest(json, &m));

    TEST_ASSERT_EQUAL_STRING("minimal", m.name);
    TEST_ASSERT_EQUAL_STRING("0.0.0", m.version);
    TEST_ASSERT_EQUAL(500, m.priority);
    TEST_ASSERT_EQUAL(0, m.hook_count);
    TEST_ASSERT_EQUAL(0, m.tool_count);

    plugin_manifest_cleanup(&m);
}

static void test_parse_manifest_invalid(void) {
    PluginManifest m;
    TEST_ASSERT_EQUAL(-1, plugin_protocol_parse_manifest("not json", &m));
    TEST_ASSERT_EQUAL(-1, plugin_protocol_parse_manifest("{}", &m));
    TEST_ASSERT_EQUAL(-1, plugin_protocol_parse_manifest(NULL, &m));
}

static void test_parse_hook_response_continue(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"action\":\"continue\",\"message\":\"modified\"}"
        ",\"id\":2}";

    HookResponse hr;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_hook_response(json, &hr));
    TEST_ASSERT_EQUAL(HOOK_CONTINUE, hr.action);
    TEST_ASSERT_NOT_NULL(hr.data);

    cJSON *msg = cJSON_GetObjectItem(hr.data, "message");
    TEST_ASSERT_EQUAL_STRING("modified", msg->valuestring);

    cJSON_Delete(hr.data);
}

static void test_parse_hook_response_stop(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"action\":\"stop\",\"result\":\"{\\\"blocked\\\":true}\"}"
        ",\"id\":3}";

    HookResponse hr;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_hook_response(json, &hr));
    TEST_ASSERT_EQUAL(HOOK_STOP, hr.action);

    cJSON_Delete(hr.data);
}

static void test_parse_hook_response_skip(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"action\":\"skip\"}"
        ",\"id\":4}";

    HookResponse hr;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_hook_response(json, &hr));
    TEST_ASSERT_EQUAL(HOOK_SKIP, hr.action);

    cJSON_Delete(hr.data);
}

static void test_parse_hook_response_error(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-1,\"message\":\"fail\"},\"id\":5}";

    HookResponse hr;
    TEST_ASSERT_EQUAL(-1, plugin_protocol_parse_hook_response(json, &hr));
}

static void test_parse_tool_result_success(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"success\":true,\"result\":\"commit log output\"}"
        ",\"id\":8}";

    PluginToolResult tr;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_tool_result(json, &tr));
    TEST_ASSERT_EQUAL(1, tr.success);
    TEST_ASSERT_EQUAL_STRING("commit log output", tr.result);

    plugin_tool_result_cleanup(&tr);
}

static void test_parse_tool_result_failure(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"result\":{"
        "\"success\":false,\"result\":\"not found\"}"
        ",\"id\":9}";

    PluginToolResult tr;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_tool_result(json, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    TEST_ASSERT_EQUAL_STRING("not found", tr.result);

    plugin_tool_result_cleanup(&tr);
}

static void test_parse_tool_result_error(void) {
    const char *json =
        "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32601,\"message\":\"Unknown tool\"},\"id\":10}";

    PluginToolResult tr;
    TEST_ASSERT_EQUAL(0, plugin_protocol_parse_tool_result(json, &tr));
    TEST_ASSERT_EQUAL(0, tr.success);
    TEST_ASSERT_EQUAL_STRING("Unknown tool", tr.result);

    plugin_tool_result_cleanup(&tr);
}

int main(void) {
    UNITY_BEGIN();

    /* Build tests */
    RUN_TEST(test_build_initialize);
    RUN_TEST(test_build_hook_event);
    RUN_TEST(test_build_hook_event_null_params);
    RUN_TEST(test_build_tool_execute);
    RUN_TEST(test_build_shutdown);

    /* Parse tests */
    RUN_TEST(test_parse_manifest_basic);
    RUN_TEST(test_parse_manifest_with_tools);
    RUN_TEST(test_parse_manifest_defaults);
    RUN_TEST(test_parse_manifest_invalid);
    RUN_TEST(test_parse_hook_response_continue);
    RUN_TEST(test_parse_hook_response_stop);
    RUN_TEST(test_parse_hook_response_skip);
    RUN_TEST(test_parse_hook_response_error);
    RUN_TEST(test_parse_tool_result_success);
    RUN_TEST(test_parse_tool_result_failure);
    RUN_TEST(test_parse_tool_result_error);

    return UNITY_END();
}
