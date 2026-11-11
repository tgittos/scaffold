#include "../unity/unity.h"
#include "util/project_scope.h"
#include "../test_fs_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_tmp[256];

static void make_dir(const char* rel) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tmp, rel);
    TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));
}

static void make_file(const char* rel) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tmp, rel);
    FILE* f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("gitdir: elsewhere\n", f);
    fclose(f);
}

static void name_for(const char* rel, char* out) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_tmp, rel);
    TEST_ASSERT_EQUAL_INT(0, project_scope_index_name_for(path, "memory", out, PROJECT_SCOPE_NAME_MAX));
}

void setUp(void) {
    snprintf(g_tmp, sizeof(g_tmp), "/tmp/test_project_scope_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(g_tmp));
    make_dir("repo_a");
    make_dir("repo_a/.git");
    make_dir("repo_a/src");
    make_dir("repo_a/src/deep");
    make_dir("repo_b");
    make_dir("repo_b/.git");
    make_dir("worktree");
    make_file("worktree/.git");
    make_dir("plain");
    make_dir("plain/sub");
}

void tearDown(void) {
    rmdir_recursive(g_tmp);
}

void test_subdirectory_shares_repo_scope(void) {
    char root[PROJECT_SCOPE_NAME_MAX], deep[PROJECT_SCOPE_NAME_MAX];
    name_for("repo_a", root);
    name_for("repo_a/src/deep", deep);
    TEST_ASSERT_EQUAL_STRING(root, deep);
}

void test_sibling_repos_differ(void) {
    char a[PROJECT_SCOPE_NAME_MAX], b[PROJECT_SCOPE_NAME_MAX];
    name_for("repo_a", a);
    name_for("repo_b", b);
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

void test_worktree_git_file_marks_root(void) {
    char wt[PROJECT_SCOPE_NAME_MAX];
    name_for("worktree", wt);
    TEST_ASSERT_EQUAL_STRING_LEN("memory_worktree_", wt, strlen("memory_worktree_"));
}

void test_no_repo_uses_directory_itself(void) {
    char plain[PROJECT_SCOPE_NAME_MAX], sub[PROJECT_SCOPE_NAME_MAX];
    name_for("plain", plain);
    name_for("plain/sub", sub);
    TEST_ASSERT_TRUE(strcmp(plain, sub) != 0);
    TEST_ASSERT_EQUAL_STRING_LEN("memory_sub_", sub, strlen("memory_sub_"));
}

void test_basename_is_sanitized(void) {
    make_dir("my repo.v2");
    char out[PROJECT_SCOPE_NAME_MAX];
    name_for("my repo.v2", out);
    TEST_ASSERT_EQUAL_STRING_LEN("memory_my_repo_v2_", out, strlen("memory_my_repo_v2_"));
}

void test_missing_dir_fails(void) {
    char out[PROJECT_SCOPE_NAME_MAX];
    TEST_ASSERT_EQUAL_INT(-1, project_scope_index_name_for("/nonexistent/xyz", "memory", out, sizeof(out)));
}

void test_small_buffer_fails(void) {
    char path[512], out[8];
    snprintf(path, sizeof(path), "%s/repo_a", g_tmp);
    TEST_ASSERT_EQUAL_INT(-1, project_scope_index_name_for(path, "memory", out, sizeof(out)));
}

void test_git_root_found_from_subdirectory(void) {
    char path[512], expected[512], root[512];
    snprintf(path, sizeof(path), "%s/repo_a/src/deep", g_tmp);
    snprintf(expected, sizeof(expected), "%s/repo_a", g_tmp);
    char resolved[512];
    TEST_ASSERT_NOT_NULL(realpath(expected, resolved));
    TEST_ASSERT_EQUAL_INT(0, project_scope_git_root(path, root, sizeof(root)));
    TEST_ASSERT_EQUAL_STRING(resolved, root);
}

void test_git_root_missing_fails(void) {
    char path[512], root[512];
    snprintf(path, sizeof(path), "%s/plain/sub", g_tmp);
    TEST_ASSERT_EQUAL_INT(-1, project_scope_git_root(path, root, sizeof(root)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_subdirectory_shares_repo_scope);
    RUN_TEST(test_sibling_repos_differ);
    RUN_TEST(test_worktree_git_file_marks_root);
    RUN_TEST(test_no_repo_uses_directory_itself);
    RUN_TEST(test_basename_is_sanitized);
    RUN_TEST(test_missing_dir_fails);
    RUN_TEST(test_small_buffer_fails);
    RUN_TEST(test_git_root_found_from_subdirectory);
    RUN_TEST(test_git_root_missing_fails);
    return UNITY_END();
}
