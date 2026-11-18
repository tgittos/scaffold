/*
 * test/tools/test_todo_tool_scope.c
 *
 * Verifies that the todo list is scoped per project: todos written from one
 * git repository are not visible from a different git repository.
 */

#include "../../test/unity/unity.h"
#include "tools/todo_tool.h"
#include "tools/todo_manager.h"
#include "tools/tools_system.h"
#include "db/task_store.h"
#include "services/services.h"
#include "util/app_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>

static char g_proj_a[PATH_MAX];
static char g_proj_b[PATH_MAX];
static char g_saved_cwd[PATH_MAX];
static char g_db_path[PATH_MAX];
static Services  *g_services  = NULL;
static TodoList   g_todo_list;
static ToolRegistry g_registry;

/* Create a minimal git repo (just the .git directory). */
static void make_git_repo(const char *path) {
    mkdir(path, 0755);
    /* Use snprintf but size the buffer relative to the actual suffix length */
    char git_dir[PATH_MAX];
    int n = snprintf(git_dir, sizeof(git_dir) - 1, "%s", path);
    if (n > 0 && n < (int)(sizeof(git_dir) - 6)) {
        memcpy(git_dir + n, "/.git", 6); /* 5 chars + NUL */
    }
    mkdir(git_dir, 0755);
}

void setUp(void) {
    char tmp_base[] = "/tmp/test_todo_scope_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmp_base));

    /* Build sub-paths from tmp_base using memcpy to avoid -Wformat-truncation.
     * tmp_base is under /tmp so all derived paths are well within PATH_MAX. */
    char home_dir[PATH_MAX];
    int base_len = (int)strlen(tmp_base);

    memcpy(home_dir, tmp_base, (size_t)base_len);
    memcpy(home_dir + base_len, "/home", 6); /* 5 + NUL */

    memcpy(g_proj_a, tmp_base, (size_t)base_len);
    memcpy(g_proj_a + base_len, "/projA", 7);

    memcpy(g_proj_b, tmp_base, (size_t)base_len);
    memcpy(g_proj_b + base_len, "/projB", 7);

    int home_len = base_len + 5; /* length of home_dir string */
    memcpy(g_db_path, home_dir, (size_t)home_len);
    memcpy(g_db_path + home_len, "/tasks.db", 10); /* 9 + NUL */

    mkdir(home_dir, 0755);
    make_git_repo(g_proj_a);
    make_git_repo(g_proj_b);

    app_home_init(home_dir);

    TEST_ASSERT_NOT_NULL(getcwd(g_saved_cwd, sizeof(g_saved_cwd)));

    g_services = services_create_empty();
    TEST_ASSERT_NOT_NULL(g_services);
    g_services->task_store = task_store_create(g_db_path);
    TEST_ASSERT_NOT_NULL(g_services->task_store);

    todo_list_init(&g_todo_list);
    init_tool_registry(&g_registry);
    g_registry.services = g_services;
}

void tearDown(void) {
    chdir(g_saved_cwd);
    clear_todo_tool_reference();
    cleanup_tool_registry(&g_registry);
    todo_list_destroy(&g_todo_list);
    if (g_services) {
        services_destroy(g_services);
        g_services = NULL;
    }
    app_home_cleanup();
}

/*
 * Write a todo with cwd = projA, then verify projB sees 0 todos, and projA
 * still sees 1 todo after switching back.
 */
void test_todos_isolated_between_projects(void) {
    /* Register with projA as cwd — initialises global state and syncs projA key */
    TEST_ASSERT_EQUAL_INT(0, chdir(g_proj_a));
    TEST_ASSERT_EQUAL_INT(0, register_todo_tool(&g_registry, &g_todo_list, g_services));

    /* Write one todo in projA */
    ToolCall write_call;
    memset(&write_call, 0, sizeof(write_call));
    write_call.id        = "tc-write";
    write_call.name      = "TodoWrite";
    write_call.arguments = "{\"todos\":[{\"content\":\"task in projA\",\"status\":\"pending\",\"priority\":\"medium\"}]}";

    ToolResult write_result;
    memset(&write_result, 0, sizeof(write_result));
    TEST_ASSERT_EQUAL_INT(0, execute_todo_tool_call(&write_call, &write_result));
    TEST_ASSERT_EQUAL_INT(1, write_result.success);
    free(write_result.tool_call_id);
    free(write_result.result);

    /* Switch to projB and read — must see 0 todos */
    TEST_ASSERT_EQUAL_INT(0, chdir(g_proj_b));

    ToolCall read_call;
    memset(&read_call, 0, sizeof(read_call));
    read_call.id        = "tc-read";
    read_call.name      = "TodoRead";
    read_call.arguments = "{}";

    ToolResult read_b;
    memset(&read_b, 0, sizeof(read_b));
    TEST_ASSERT_EQUAL_INT(0, execute_todo_tool_call(&read_call, &read_b));
    TEST_ASSERT_EQUAL_UINT(0, (unsigned)g_todo_list.count);
    free(read_b.tool_call_id);
    free(read_b.result);

    /* Switch back to projA and read — must see 1 todo */
    TEST_ASSERT_EQUAL_INT(0, chdir(g_proj_a));

    ToolResult read_a;
    memset(&read_a, 0, sizeof(read_a));
    TEST_ASSERT_EQUAL_INT(0, execute_todo_tool_call(&read_call, &read_a));
    TEST_ASSERT_EQUAL_UINT(1, (unsigned)g_todo_list.count);
    free(read_a.tool_call_id);
    free(read_a.result);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_todos_isolated_between_projects);
    return UNITY_END();
}
