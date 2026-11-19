#include "../unity/unity.h"
#include "agent/change_report.h"
#include "../test_fs_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char g_tmpdir[256];
static char g_original_dir[1024];

/* Run a shell command, return 0 on success. */
static int sh(const char *fmt, ...) {
    char cmd[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    return system(cmd);
}

/* Run cmd via popen, copy first line (stripped of newline) to buf. */
static int popen_line(const char *cmd, char *buf, size_t size) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    if (!fgets(buf, (int)size, fp)) { pclose(fp); return -1; }
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
    pclose(fp);
    return 0;
}

void setUp(void) {
    if (getcwd(g_original_dir, sizeof(g_original_dir)) == NULL) {
        TEST_FAIL_MESSAGE("Failed to get cwd");
    }
    snprintf(g_tmpdir, sizeof(g_tmpdir), "/tmp/test_change_report_XXXXXX");
    if (mkdtemp(g_tmpdir) == NULL) {
        TEST_FAIL_MESSAGE("Failed to create temp directory");
    }
}

void tearDown(void) {
    chdir(g_original_dir);
    rmdir_recursive(g_tmpdir);
}

/* -------------------------------------------------------------------------
 * test_report_lists_new_commit
 * Set up a git repo, make an initial commit, capture HEAD as head_before,
 * make a second commit, then verify the report contains the new hash/subject.
 * ------------------------------------------------------------------------- */
void test_report_lists_new_commit(void) {
    /* Init repo and configure identity. */
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' init -q", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' config user.email 'test@example.com'", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' config user.name 'Test'", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' config commit.gpgsign false", g_tmpdir));

    /* Initial commit (this will be head_before). */
    char initial_file[512];
    snprintf(initial_file, sizeof(initial_file), "%s/init.txt", g_tmpdir);
    FILE *f = fopen(initial_file, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("initial\n", f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' add init.txt", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' commit -q -m 'initial commit'", g_tmpdir));

    /* Capture head_before = current HEAD. */
    char head_before[64] = {0};
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse HEAD 2>/dev/null", g_tmpdir);
    TEST_ASSERT_EQUAL_INT(0, popen_line(cmd, head_before, sizeof(head_before)));
    TEST_ASSERT_TRUE(head_before[0] != '\0');

    /* Make a second commit. */
    char new_file[512];
    snprintf(new_file, sizeof(new_file), "%s/feature.txt", g_tmpdir);
    f = fopen(new_file, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("hello world\n", f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' add feature.txt", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' commit -q -m 'add feature'", g_tmpdir));

    /* Collect report. */
    change_report_t *report = change_report_collect(g_tmpdir, head_before);
    TEST_ASSERT_NOT_NULL(report);

    /* Should have exactly one commit. */
    TEST_ASSERT_EQUAL_INT(1, report->commit_count);

    /* Should mention the new commit subject. */
    TEST_ASSERT_NOT_NULL(strstr(report->commits_text, "add feature"));

    /* File stats should be non-zero. */
    TEST_ASSERT_GREATER_THAN(0, report->files_changed);

    /* Obtain the short hash of the new commit to verify it appears in the report. */
    char short_hash[16] = {0};
    snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse --short HEAD 2>/dev/null", g_tmpdir);
    TEST_ASSERT_EQUAL_INT(0, popen_line(cmd, short_hash, sizeof(short_hash)));
    TEST_ASSERT_TRUE(short_hash[0] != '\0');

    /* Short hash should appear in commits_text. */
    TEST_ASSERT_NOT_NULL(strstr(report->commits_text, short_hash));

    /* Format and verify text representation. */
    char *text = change_report_format(report);
    TEST_ASSERT_NOT_NULL(text);
    TEST_ASSERT_NOT_NULL(strstr(text, "Changed:"));
    TEST_ASSERT_NOT_NULL(strstr(text, "Commits:"));
    TEST_ASSERT_NOT_NULL(strstr(text, "add feature"));

    /* Verify the Commits line starts with the short hash. */
    char commits_prefix[32];
    snprintf(commits_prefix, sizeof(commits_prefix), "Commits: %s", short_hash);
    TEST_ASSERT_NOT_NULL(strstr(text, commits_prefix));
    free(text);

    change_report_free(report);
}

/* -------------------------------------------------------------------------
 * test_report_null_when_unchanged
 * When head_before == HEAD, no commits exist → NULL.
 * ------------------------------------------------------------------------- */
void test_report_null_when_unchanged(void) {
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' init -q", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' config user.email 'test@example.com'", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' config user.name 'Test'", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' config commit.gpgsign false", g_tmpdir));

    /* Make a commit. */
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/readme.txt", g_tmpdir);
    FILE *f = fopen(file_path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("readme\n", f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' add readme.txt", g_tmpdir));
    TEST_ASSERT_EQUAL_INT(0, sh("git -C '%s' commit -q -m 'initial'", g_tmpdir));

    /* Capture HEAD as head_before (nothing new will happen after this). */
    char head_before[64] = {0};
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse HEAD 2>/dev/null", g_tmpdir);
    TEST_ASSERT_EQUAL_INT(0, popen_line(cmd, head_before, sizeof(head_before)));

    /* No new commits — expect NULL. */
    change_report_t *report = change_report_collect(g_tmpdir, head_before);
    TEST_ASSERT_NULL(report);
}

/* -------------------------------------------------------------------------
 * test_report_non_git_dir_returns_null
 * A plain directory with no git repo → NULL.
 * ------------------------------------------------------------------------- */
void test_report_non_git_dir_returns_null(void) {
    /* g_tmpdir has no .git directory. */
    change_report_t *report = change_report_collect(g_tmpdir, "HEAD");
    TEST_ASSERT_NULL(report);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_report_lists_new_commit);
    RUN_TEST(test_report_null_when_unchanged);
    RUN_TEST(test_report_non_git_dir_returns_null);
    return UNITY_END();
}
