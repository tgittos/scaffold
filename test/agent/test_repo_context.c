#include "../../test/unity/unity.h"
#include "agent/session.h"
#include "agent/context_enhancement.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static AgentSession g_session;

void setUp(void) {
    memset(&g_session, 0, sizeof(g_session));
    g_session.session_data.config.system_prompt = strdup("Base prompt.");
    g_session.current_mode = PROMPT_MODE_DEFAULT;
    g_session.first_turn_context_injected = 0;
    todo_list_init(&g_session.todo_list);
}

void tearDown(void) {
    free(g_session.session_data.config.system_prompt);
    todo_list_destroy(&g_session.todo_list);
}

void test_repo_snapshot_returns_non_null_in_git_repo(void) {
    /* We are running inside a git repo */
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_TRUE(strlen(snapshot) > 0);
    free(snapshot);
}

void test_repo_snapshot_contains_header(void) {
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_NOT_NULL(strstr(snapshot, "# Repository Context"));
    free(snapshot);
}

void test_repo_snapshot_contains_branch(void) {
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_NOT_NULL(strstr(snapshot, "Branch:"));
    free(snapshot);
}

void test_repo_snapshot_contains_commits(void) {
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_NOT_NULL(strstr(snapshot, "Recent commits:"));
    free(snapshot);
}

void test_repo_snapshot_contains_project_root(void) {
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    /* This repo has a Makefile */
    TEST_ASSERT_NOT_NULL(strstr(snapshot, "Makefile"));
    free(snapshot);
}

void test_repo_snapshot_contains_directory_structure(void) {
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_NOT_NULL(strstr(snapshot, "# Directory Structure"));
    free(snapshot);
}

void test_repo_snapshot_within_size_limit(void) {
    char* snapshot = build_repo_snapshot();
    TEST_ASSERT_NOT_NULL(snapshot);
    TEST_ASSERT_TRUE(strlen(snapshot) < 2048);
    free(snapshot);
}

void test_directory_tree_null_root(void) {
    char* tree = build_directory_tree(NULL, 2);
    TEST_ASSERT_NULL(tree);
}

void test_directory_tree_nonexistent_dir(void) {
    char* tree = build_directory_tree("/nonexistent_dir_xyz_12345", 2);
    TEST_ASSERT_NULL(tree);
}

void test_directory_tree_depth_zero(void) {
    /* Create temp dir structure */
    char tmpdir[] = "/tmp/test_tree_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char subdir[256];
    snprintf(subdir, sizeof(subdir), "%s/aaa", tmpdir);
    mkdir(subdir, 0755);
    char nested[256];
    snprintf(nested, sizeof(nested), "%s/aaa/bbb", tmpdir);
    mkdir(nested, 0755);

    char* tree = build_directory_tree(tmpdir, 0);
    TEST_ASSERT_NOT_NULL(tree);
    /* Should see top-level dir but not nested */
    TEST_ASSERT_NOT_NULL(strstr(tree, "aaa/"));
    TEST_ASSERT_NULL(strstr(tree, "bbb/"));
    free(tree);

    /* Cleanup */
    rmdir(nested);
    rmdir(subdir);
    rmdir(tmpdir);
}

void test_directory_tree_depth_limit(void) {
    char tmpdir[] = "/tmp/test_tree2_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char d1[256], d2[256], d3[256];
    snprintf(d1, sizeof(d1), "%s/level1", tmpdir);
    snprintf(d2, sizeof(d2), "%s/level1/level2", tmpdir);
    snprintf(d3, sizeof(d3), "%s/level1/level2/level3", tmpdir);
    mkdir(d1, 0755);
    mkdir(d2, 0755);
    mkdir(d3, 0755);

    /* depth=1: should see level1 and level2, but not level3 */
    char* tree = build_directory_tree(tmpdir, 1);
    TEST_ASSERT_NOT_NULL(tree);
    TEST_ASSERT_NOT_NULL(strstr(tree, "level1/"));
    TEST_ASSERT_NOT_NULL(strstr(tree, "level2/"));
    TEST_ASSERT_NULL(strstr(tree, "level3/"));
    free(tree);

    rmdir(d3);
    rmdir(d2);
    rmdir(d1);
    rmdir(tmpdir);
}

void test_directory_tree_skips_dotgit(void) {
    char tmpdir[] = "/tmp/test_tree3_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char gitdir[256], srcdir[256];
    snprintf(gitdir, sizeof(gitdir), "%s/.git", tmpdir);
    snprintf(srcdir, sizeof(srcdir), "%s/src", tmpdir);
    mkdir(gitdir, 0755);
    mkdir(srcdir, 0755);

    char* tree = build_directory_tree(tmpdir, 2);
    TEST_ASSERT_NOT_NULL(tree);
    TEST_ASSERT_NOT_NULL(strstr(tree, "src/"));
    TEST_ASSERT_NULL(strstr(tree, ".git/"));
    free(tree);

    rmdir(gitdir);
    rmdir(srcdir);
    rmdir(tmpdir);
}

void test_directory_tree_skips_node_modules(void) {
    char tmpdir[] = "/tmp/test_tree4_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpdir));

    char nmdir[256], libdir[256];
    snprintf(nmdir, sizeof(nmdir), "%s/node_modules", tmpdir);
    snprintf(libdir, sizeof(libdir), "%s/lib", tmpdir);
    mkdir(nmdir, 0755);
    mkdir(libdir, 0755);

    char* tree = build_directory_tree(tmpdir, 2);
    TEST_ASSERT_NOT_NULL(tree);
    TEST_ASSERT_NOT_NULL(strstr(tree, "lib/"));
    TEST_ASSERT_NULL(strstr(tree, "node_modules/"));
    free(tree);

    rmdir(nmdir);
    rmdir(libdir);
    rmdir(tmpdir);
}

void test_first_turn_flag_injects_repo_context(void) {
    g_session.first_turn_context_injected = 0;

    EnhancedPromptParts parts;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* First turn should have repo context */
    TEST_ASSERT_NOT_NULL(parts.dynamic_context);
    TEST_ASSERT_NOT_NULL(strstr(parts.dynamic_context, "Repository Context"));

    /* Flag should now be set */
    TEST_ASSERT_EQUAL_INT(1, g_session.first_turn_context_injected);

    free_enhanced_prompt_parts(&parts);
}

void test_second_turn_no_repo_context(void) {
    /* Simulate first turn */
    g_session.first_turn_context_injected = 0;
    EnhancedPromptParts parts1;
    build_enhanced_prompt_parts(&g_session, NULL, &parts1);
    free_enhanced_prompt_parts(&parts1);

    /* Second turn */
    EnhancedPromptParts parts2;
    int rc = build_enhanced_prompt_parts(&g_session, NULL, &parts2);
    TEST_ASSERT_EQUAL_INT(0, rc);

    /* Should NOT have repo context */
    if (parts2.dynamic_context != NULL) {
        TEST_ASSERT_NULL(strstr(parts2.dynamic_context, "Repository Context"));
    }

    free_enhanced_prompt_parts(&parts2);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_repo_snapshot_returns_non_null_in_git_repo);
    RUN_TEST(test_repo_snapshot_contains_header);
    RUN_TEST(test_repo_snapshot_contains_branch);
    RUN_TEST(test_repo_snapshot_contains_commits);
    RUN_TEST(test_repo_snapshot_contains_project_root);
    RUN_TEST(test_repo_snapshot_contains_directory_structure);
    RUN_TEST(test_repo_snapshot_within_size_limit);
    RUN_TEST(test_directory_tree_null_root);
    RUN_TEST(test_directory_tree_nonexistent_dir);
    RUN_TEST(test_directory_tree_depth_zero);
    RUN_TEST(test_directory_tree_depth_limit);
    RUN_TEST(test_directory_tree_skips_dotgit);
    RUN_TEST(test_directory_tree_skips_node_modules);
    RUN_TEST(test_first_turn_flag_injects_repo_context);
    RUN_TEST(test_second_turn_no_repo_context);

    return UNITY_END();
}
