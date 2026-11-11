#include "../unity/unity.h"
#include "policy/auto_approve.h"
#include "types.h"
#include "../test_fs_utils.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_tmp[256];
static char g_saved_cwd[PATH_MAX];

static void make_dir(const char *rel) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tmp, rel);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));
}

void setUp(void) {
    TEST_ASSERT_NOT_NULL(getcwd(g_saved_cwd, sizeof(g_saved_cwd)));
    snprintf(g_tmp, sizeof(g_tmp), "/tmp/test_auto_approve_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_tmp));
    make_dir("repo");
    make_dir("repo/.git");
    make_dir("repo/src");
    make_dir("outside");
    char repo[512];
    snprintf(repo, sizeof(repo), "%s/repo", g_tmp);
    TEST_ASSERT_EQUAL_INT(0, chdir(repo));
}

void tearDown(void) {
    TEST_ASSERT_EQUAL_INT(0, chdir(g_saved_cwd));
    rmdir_recursive(g_tmp);
}

static int approve_shell(const char *command) {
    char args[1024];
    snprintf(args, sizeof(args), "{\"command\": \"%s\"}", command);
    ToolCall call = { .id = "c", .name = "shell", .arguments = args };
    return auto_approve_without_review(&call);
}

static int approve_write(const char *path) {
    char args[1024];
    snprintf(args, sizeof(args), "{\"path\": \"%s\", \"content\": \"x\"}", path);
    ToolCall call = { .id = "c", .name = "write_file", .arguments = args };
    return auto_approve_without_review(&call);
}

static int approve_patch(const char *patch) {
    char args[2048];
    snprintf(args, sizeof(args), "{\"patch\": \"%s\"}", patch);
    ToolCall call = { .id = "c", .name = "apply_patch", .arguments = args };
    return auto_approve_without_review(&call);
}

void test_read_only_shell_commands_approved(void) {
    TEST_ASSERT_EQUAL_INT(1, approve_shell("git status && ls -la | head -20"));
    TEST_ASSERT_EQUAL_INT(1, approve_shell("grep -rn foo src 2>/dev/null | wc -l"));
    TEST_ASSERT_EQUAL_INT(1, approve_shell("cd src && git log --oneline -5"));
    TEST_ASSERT_EQUAL_INT(1, approve_shell("find . -name '*.c'"));
}

void test_writing_shell_commands_need_review(void) {
    TEST_ASSERT_EQUAL_INT(0, approve_shell("git push"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("ls > out.txt"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("cat script | sh"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("find . -name x -delete"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("find . -exec touch {} +"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("git -C .. status"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("git diff --output=patch.txt"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("echo $(rm x)"));
    TEST_ASSERT_EQUAL_INT(0, approve_shell("ls\\nmake install"));
}

void test_write_inside_project_approved(void) {
    TEST_ASSERT_EQUAL_INT(1, approve_write("src/a.c"));
    TEST_ASSERT_EQUAL_INT(1, approve_write("newdir/nested/b.c"));

    char absolute[512];
    snprintf(absolute, sizeof(absolute), "%s/repo/src/c.c", g_tmp);
    TEST_ASSERT_EQUAL_INT(1, approve_write(absolute));
}

void test_write_outside_project_needs_review(void) {
    TEST_ASSERT_EQUAL_INT(0, approve_write("../outside/a.c"));
    TEST_ASSERT_EQUAL_INT(0, approve_write("newdir/../../outside/a.c"));
    TEST_ASSERT_EQUAL_INT(0, approve_write("/etc/hosts"));
    TEST_ASSERT_EQUAL_INT(0, approve_write(".git/config"));
    TEST_ASSERT_EQUAL_INT(0, approve_write(".env"));
}

void test_write_through_symlink_out_of_project_needs_review(void) {
    char target[512];
    snprintf(target, sizeof(target), "%s/outside", g_tmp);
    TEST_ASSERT_EQUAL_INT(0, symlink(target, "link"));
    TEST_ASSERT_EQUAL_INT(0, approve_write("link/a.c"));
}

void test_write_without_git_project_needs_review(void) {
    char outside[512];
    snprintf(outside, sizeof(outside), "%s/outside", g_tmp);
    TEST_ASSERT_EQUAL_INT(0, chdir(outside));
    TEST_ASSERT_EQUAL_INT(0, approve_write("a.c"));
}

void test_patch_inside_project_approved(void) {
    TEST_ASSERT_EQUAL_INT(1, approve_patch(
        "*** Begin Patch\\n*** Update File: src/a.c\\n@@\\n-x\\n+y\\n"
        "*** Add File: src/b.c\\n+z\\n*** End Patch"));
}

void test_patch_touching_outside_file_needs_review(void) {
    TEST_ASSERT_EQUAL_INT(0, approve_patch(
        "*** Begin Patch\\n*** Update File: src/a.c\\n@@\\n-x\\n+y\\n"
        "*** Delete File: ../outside/a.c\\n*** End Patch"));
    TEST_ASSERT_EQUAL_INT(0, approve_patch(
        "*** Begin Patch\\n*** Update File: src/a.c\\n*** Move to: ../outside/a.c\\n*** End Patch"));
    TEST_ASSERT_EQUAL_INT(0, approve_patch("*** Begin Patch\\n*** End Patch"));
}

void test_other_tools_need_review(void) {
    ToolCall fetch = { .id = "c", .name = "web_fetch", .arguments = "{\"url\": \"https://x\"}" };
    TEST_ASSERT_EQUAL_INT(0, auto_approve_without_review(&fetch));
    TEST_ASSERT_EQUAL_INT(0, auto_approve_without_review(NULL));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_only_shell_commands_approved);
    RUN_TEST(test_writing_shell_commands_need_review);
    RUN_TEST(test_write_inside_project_approved);
    RUN_TEST(test_write_outside_project_needs_review);
    RUN_TEST(test_write_through_symlink_out_of_project_needs_review);
    RUN_TEST(test_write_without_git_project_needs_review);
    RUN_TEST(test_patch_inside_project_approved);
    RUN_TEST(test_patch_touching_outside_file_needs_review);
    RUN_TEST(test_other_tools_need_review);
    return UNITY_END();
}
