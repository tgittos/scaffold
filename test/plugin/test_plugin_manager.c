#include "unity/unity.h"
#include "plugin/plugin_manager.h"
#include "plugin/plugin_protocol.h"
#include "util/app_home.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: join two path segments via malloc */
static char *join_path(const char *a, const char *b) {
    size_t len = strlen(a) + 1 + strlen(b) + 1;
    char *out = malloc(len);
    if (out) sprintf(out, "%s/%s", a, b);
    return out;
}

/* --- Init tests --- */

static void test_init_zeroes(void) {
    PluginManager mgr;
    mgr.count = 42;
    plugin_manager_init(&mgr);
    TEST_ASSERT_EQUAL(0, mgr.count);
    for (int i = 0; i < MAX_PLUGINS; i++) {
        TEST_ASSERT_NULL(mgr.plugins[i].path);
        TEST_ASSERT_EQUAL(0, mgr.plugins[i].pid);
        TEST_ASSERT_EQUAL(0, mgr.plugins[i].initialized);
    }
}

static void test_init_null(void) {
    plugin_manager_init(NULL);
}

/* --- Discover tests --- */

static void test_discover_missing_dir(void) {
    char tmpdir[] = "/tmp/scaffold_test_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    PluginManager mgr;
    plugin_manager_init(&mgr);
    TEST_ASSERT_EQUAL(0, plugin_manager_discover(&mgr));
    TEST_ASSERT_EQUAL(0, mgr.count);

    rmdir(tmpdir);
}

static void test_discover_empty_dir(void) {
    char tmpdir[] = "/tmp/scaffold_test_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    char *plugins_path = join_path(tmpdir, "plugins");
    mkdir(plugins_path, 0755);

    PluginManager mgr;
    plugin_manager_init(&mgr);
    TEST_ASSERT_EQUAL(0, plugin_manager_discover(&mgr));

    rmdir(plugins_path);
    free(plugins_path);
    rmdir(tmpdir);
}

static void test_discover_finds_executables(void) {
    char tmpdir[] = "/tmp/scaffold_test_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    char *plugins_path = join_path(tmpdir, "plugins");
    mkdir(plugins_path, 0755);

    char *exe_path = join_path(plugins_path, "test-plugin");
    FILE *f = fopen(exe_path, "w");
    fprintf(f, "#!/bin/sh\n");
    fclose(f);
    chmod(exe_path, 0755);

    char *noexe_path = join_path(plugins_path, "readme.txt");
    f = fopen(noexe_path, "w");
    fprintf(f, "not a plugin\n");
    fclose(f);

    PluginManager mgr;
    plugin_manager_init(&mgr);
    TEST_ASSERT_EQUAL(1, plugin_manager_discover(&mgr));
    TEST_ASSERT_EQUAL(1, mgr.count);
    TEST_ASSERT_NOT_NULL(mgr.plugins[0].path);
    TEST_ASSERT_NOT_NULL(strstr(mgr.plugins[0].path, "test-plugin"));

    free(mgr.plugins[0].path);
    unlink(exe_path);
    unlink(noexe_path);
    free(exe_path);
    free(noexe_path);
    rmdir(plugins_path);
    free(plugins_path);
    rmdir(tmpdir);
}

static void test_discover_skips_hidden(void) {
    char tmpdir[] = "/tmp/scaffold_test_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    char *plugins_path = join_path(tmpdir, "plugins");
    mkdir(plugins_path, 0755);

    char *hidden_path = join_path(plugins_path, ".hidden-plugin");
    FILE *f = fopen(hidden_path, "w");
    fprintf(f, "#!/bin/sh\n");
    fclose(f);
    chmod(hidden_path, 0755);

    PluginManager mgr;
    plugin_manager_init(&mgr);
    TEST_ASSERT_EQUAL(0, plugin_manager_discover(&mgr));

    unlink(hidden_path);
    free(hidden_path);
    rmdir(plugins_path);
    free(plugins_path);
    rmdir(tmpdir);
}

static void test_discover_skips_symlinks(void) {
    char tmpdir[] = "/tmp/scaffold_test_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    char *plugins_path = join_path(tmpdir, "plugins");
    mkdir(plugins_path, 0755);

    /* Create a real executable */
    char *real_path = join_path(tmpdir, "real-plugin");
    FILE *f = fopen(real_path, "w");
    fprintf(f, "#!/bin/sh\n");
    fclose(f);
    chmod(real_path, 0755);

    /* Create a symlink to it inside plugins dir */
    char *link_path = join_path(plugins_path, "link-plugin");
    symlink(real_path, link_path);

    PluginManager mgr;
    plugin_manager_init(&mgr);
    TEST_ASSERT_EQUAL(0, plugin_manager_discover(&mgr));
    TEST_ASSERT_EQUAL(0, mgr.count);

    unlink(link_path);
    unlink(real_path);
    free(link_path);
    free(real_path);
    rmdir(plugins_path);
    free(plugins_path);
    rmdir(tmpdir);
}

/* --- Shutdown with no plugins --- */

static void test_shutdown_empty(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);
    plugin_manager_shutdown_all(&mgr);
    TEST_ASSERT_EQUAL(0, mgr.count);
}

static void test_shutdown_null(void) {
    plugin_manager_shutdown_all(NULL);
}

/* --- Send request with bad FDs --- */

static void test_send_request_bad_fds(void) {
    PluginProcess p;
    memset(&p, 0, sizeof(p));
    p.stdin_fd = -1;
    p.stdout_fd = -1;

    char *response = NULL;
    TEST_ASSERT_EQUAL(-1, plugin_manager_send_request(&p, "{}", &response));
    TEST_ASSERT_NULL(response);
}

/* --- Execute tool with no manager --- */

static void test_execute_tool_no_manager(void) {
    ToolCall call = { .id = "1", .name = "plugin_foo_bar", .arguments = "{}" };
    ToolResult result = {0};
    TEST_ASSERT_EQUAL(-1, plugin_manager_execute_tool(NULL, &call, &result));
}

static void test_execute_tool_not_plugin_name(void) {
    PluginManager mgr;
    plugin_manager_init(&mgr);

    ToolCall call = { .id = "1", .name = "regular_tool", .arguments = "{}" };
    ToolResult result = {0};
    TEST_ASSERT_EQUAL(-1, plugin_manager_execute_tool(&mgr, &call, &result));
}

/* --- Name validation tests --- */

static void test_validate_name_valid(void) {
    TEST_ASSERT_EQUAL(0, plugin_validate_name("myplugin"));
    TEST_ASSERT_EQUAL(0, plugin_validate_name("a"));
    TEST_ASSERT_EQUAL(0, plugin_validate_name("my-plugin"));
    TEST_ASSERT_EQUAL(0, plugin_validate_name("Plugin123"));
    TEST_ASSERT_EQUAL(0, plugin_validate_name("x"));
}

static void test_validate_name_null(void) {
    TEST_ASSERT_EQUAL(-1, plugin_validate_name(NULL));
}

static void test_validate_name_empty(void) {
    TEST_ASSERT_EQUAL(-1, plugin_validate_name(""));
}

static void test_validate_name_with_underscore(void) {
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("my_plugin"));
}

static void test_validate_name_with_slash(void) {
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("my/plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("my\\plugin"));
}

static void test_validate_name_rejects_special_chars(void) {
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("bad plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("bad;plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("bad.plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("bad@plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("\"; rm -rf /"));
}

static void test_validate_name_must_start_with_letter(void) {
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("1plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("-plugin"));
    TEST_ASSERT_EQUAL(-1, plugin_validate_name("0"));
}

static void test_validate_name_too_long(void) {
    char long_name[66];
    memset(long_name, 'a', 65);
    long_name[65] = '\0';
    TEST_ASSERT_EQUAL(-1, plugin_validate_name(long_name));

    long_name[64] = '\0';
    TEST_ASSERT_EQUAL(0, plugin_validate_name(long_name));
}

/* --- Alive check tests --- */

static void test_check_alive_not_initialized(void) {
    PluginProcess p;
    memset(&p, 0, sizeof(p));
    p.pid = 0;
    p.initialized = 0;
    TEST_ASSERT_EQUAL(0, plugin_check_alive(&p));
}

static void test_check_alive_null(void) {
    TEST_ASSERT_EQUAL(0, plugin_check_alive(NULL));
}

static void test_check_alive_dead_pid(void) {
    PluginProcess p;
    memset(&p, 0, sizeof(p));
    p.pid = 999999;
    p.initialized = 1;
    p.stdin_fd = -1;
    p.stdout_fd = -1;
    TEST_ASSERT_EQUAL(0, plugin_check_alive(&p));
    TEST_ASSERT_EQUAL(0, p.initialized);
    TEST_ASSERT_EQUAL(0, p.pid);
}

/* --- Get plugins dir --- */

static void test_get_plugins_dir(void) {
    char tmpdir[] = "/tmp/scaffold_test_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));
    app_home_init(tmpdir);

    char *dir = plugin_manager_get_plugins_dir();
    TEST_ASSERT_NOT_NULL(dir);
    TEST_ASSERT_NOT_NULL(strstr(dir, "plugins"));
    free(dir);

    rmdir(tmpdir);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_zeroes);
    RUN_TEST(test_init_null);
    RUN_TEST(test_discover_missing_dir);
    RUN_TEST(test_discover_empty_dir);
    RUN_TEST(test_discover_finds_executables);
    RUN_TEST(test_discover_skips_hidden);
    RUN_TEST(test_discover_skips_symlinks);
    RUN_TEST(test_shutdown_empty);
    RUN_TEST(test_shutdown_null);
    RUN_TEST(test_send_request_bad_fds);
    RUN_TEST(test_execute_tool_no_manager);
    RUN_TEST(test_execute_tool_not_plugin_name);
    RUN_TEST(test_get_plugins_dir);

    RUN_TEST(test_validate_name_valid);
    RUN_TEST(test_validate_name_null);
    RUN_TEST(test_validate_name_empty);
    RUN_TEST(test_validate_name_with_underscore);
    RUN_TEST(test_validate_name_with_slash);
    RUN_TEST(test_validate_name_too_long);
    RUN_TEST(test_check_alive_not_initialized);
    RUN_TEST(test_check_alive_null);
    RUN_TEST(test_check_alive_dead_pid);
    RUN_TEST(test_validate_name_rejects_special_chars);
    RUN_TEST(test_validate_name_must_start_with_letter);

    return UNITY_END();
}
