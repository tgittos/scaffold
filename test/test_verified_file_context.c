/**
 * Unit tests for verified file context module.
 *
 * Tests TOCTOU-safe file access functionality that enables Python tools
 * to use verified file descriptors instead of direct open().
 */

#include "unity.h"
#include "policy/verified_file_context.h"
#include "policy/atomic_file.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

/* Test file paths */
static char test_file_path[512];
static char test_dir_path[256];

void setUp(void) {
    /* Create test directory and file */
    snprintf(test_dir_path, sizeof(test_dir_path), "/tmp/ralph_vfc_test_%d", getpid());
    snprintf(test_file_path, sizeof(test_file_path), "%s/test.txt", test_dir_path);

    mkdir(test_dir_path, 0755);

    /* Create test file with content */
    int fd = open(test_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        const char *content = "test content\n";
        write(fd, content, strlen(content));
        close(fd);
    }

    /* Ensure no context is set */
    verified_file_context_clear();
}

void tearDown(void) {
    /* Clear context */
    verified_file_context_clear();

    /* Clean up test files */
    unlink(test_file_path);
    rmdir(test_dir_path);
}

/* Test that no context is set initially */
void test_verified_file_context_not_set_initially(void) {
    TEST_ASSERT_FALSE(verified_file_context_is_set());
    TEST_ASSERT_NULL(verified_file_context_get_resolved_path());
}

/* Test setting context with valid approved path */
void test_verified_file_context_set_valid(void) {
    ApprovedPath approved;
    init_approved_path(&approved);

    int result = capture_approved_path(test_file_path, &approved);
    TEST_ASSERT_EQUAL_INT(0, result);

    result = verified_file_context_set(&approved);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(verified_file_context_is_set());

    free_approved_path(&approved);
}

/* Test that set fails with NULL */
void test_verified_file_context_set_null_fails(void) {
    int result = verified_file_context_set(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_FALSE(verified_file_context_is_set());
}

/* Test clearing context */
void test_verified_file_context_clear(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);
    free_approved_path(&approved);

    TEST_ASSERT_TRUE(verified_file_context_is_set());

    verified_file_context_clear();
    TEST_ASSERT_FALSE(verified_file_context_is_set());
    TEST_ASSERT_NULL(verified_file_context_get_resolved_path());
}

/* Test clearing when no context is set (should not crash) */
void test_verified_file_context_clear_when_not_set(void) {
    verified_file_context_clear();  /* Should not crash */
    TEST_ASSERT_FALSE(verified_file_context_is_set());
}

/* Test get_resolved_path returns correct path */
void test_verified_file_context_get_resolved_path(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    const char *resolved = verified_file_context_get_resolved_path();
    TEST_ASSERT_NOT_NULL(resolved);
    TEST_ASSERT_NOT_NULL(strstr(resolved, "test.txt"));

    free_approved_path(&approved);
}

/* Test path_matches with exact match */
void test_verified_file_context_path_matches_exact(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    TEST_ASSERT_TRUE(verified_file_context_path_matches(test_file_path));

    free_approved_path(&approved);
}

/* Test path_matches with non-matching path */
void test_verified_file_context_path_matches_different(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    TEST_ASSERT_FALSE(verified_file_context_path_matches("/tmp/other_file.txt"));

    free_approved_path(&approved);
}

/* Test path_matches with NULL path */
void test_verified_file_context_path_matches_null(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    TEST_ASSERT_FALSE(verified_file_context_path_matches(NULL));

    free_approved_path(&approved);
}

/* Test get_fd for read mode */
void test_verified_file_context_get_fd_read(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    int fd = -1;
    VerifyResult result = verified_file_context_get_fd(test_file_path, VERIFIED_MODE_READ, &fd);

    TEST_ASSERT_EQUAL_INT(VERIFY_OK, result);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Verify we can read from the fd */
    char buffer[64];
    ssize_t bytes = read(fd, buffer, sizeof(buffer) - 1);
    TEST_ASSERT_TRUE(bytes > 0);
    buffer[bytes] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(buffer, "test content"));

    close(fd);
    free_approved_path(&approved);
}

/* Test get_fd for write mode */
void test_verified_file_context_get_fd_write(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    int fd = -1;
    VerifyResult result = verified_file_context_get_fd(test_file_path, VERIFIED_MODE_WRITE, &fd);

    TEST_ASSERT_EQUAL_INT(VERIFY_OK, result);
    TEST_ASSERT_TRUE(fd >= 0);

    /* Write something */
    const char *new_content = "new content\n";
    ssize_t written = write(fd, new_content, strlen(new_content));
    TEST_ASSERT_EQUAL_INT(strlen(new_content), written);

    close(fd);
    free_approved_path(&approved);
}

/* Test get_fd without context set - falls back to regular open */
void test_verified_file_context_get_fd_no_context(void) {
    int fd = -1;
    VerifyResult result = verified_file_context_get_fd(test_file_path, VERIFIED_MODE_READ, &fd);

    TEST_ASSERT_EQUAL_INT(VERIFY_OK, result);
    TEST_ASSERT_TRUE(fd >= 0);

    close(fd);
}

/* Test get_fd with NULL out_fd */
void test_verified_file_context_get_fd_null_out_fd(void) {
    VerifyResult result = verified_file_context_get_fd(test_file_path, VERIFIED_MODE_READ, NULL);
    TEST_ASSERT_NOT_EQUAL_INT(VERIFY_OK, result);
}

/* Test get_fd with NULL path */
void test_verified_file_context_get_fd_null_path(void) {
    int fd = -1;
    VerifyResult result = verified_file_context_get_fd(NULL, VERIFIED_MODE_READ, &fd);
    TEST_ASSERT_NOT_EQUAL_INT(VERIFY_OK, result);
    TEST_ASSERT_EQUAL_INT(-1, fd);
}

/* Test get_fd with path mismatch */
void test_verified_file_context_get_fd_path_mismatch(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    int fd = -1;
    VerifyResult result = verified_file_context_get_fd("/tmp/other_file.txt", VERIFIED_MODE_READ, &fd);

    /* Should fail because path doesn't match approved path */
    TEST_ASSERT_NOT_EQUAL_INT(VERIFY_OK, result);
    TEST_ASSERT_EQUAL_INT(-1, fd);

    free_approved_path(&approved);
}

/* Test context is properly copied (not just referenced) */
void test_verified_file_context_copies_data(void) {
    ApprovedPath approved;
    init_approved_path(&approved);
    capture_approved_path(test_file_path, &approved);
    verified_file_context_set(&approved);

    /* Free the original - context should still work */
    free_approved_path(&approved);

    /* Context should still be valid */
    TEST_ASSERT_TRUE(verified_file_context_is_set());
    TEST_ASSERT_NOT_NULL(verified_file_context_get_resolved_path());
    TEST_ASSERT_TRUE(verified_file_context_path_matches(test_file_path));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_verified_file_context_not_set_initially);
    RUN_TEST(test_verified_file_context_set_valid);
    RUN_TEST(test_verified_file_context_set_null_fails);
    RUN_TEST(test_verified_file_context_clear);
    RUN_TEST(test_verified_file_context_clear_when_not_set);
    RUN_TEST(test_verified_file_context_get_resolved_path);
    RUN_TEST(test_verified_file_context_path_matches_exact);
    RUN_TEST(test_verified_file_context_path_matches_different);
    RUN_TEST(test_verified_file_context_path_matches_null);
    RUN_TEST(test_verified_file_context_get_fd_read);
    RUN_TEST(test_verified_file_context_get_fd_write);
    RUN_TEST(test_verified_file_context_get_fd_no_context);
    RUN_TEST(test_verified_file_context_get_fd_null_out_fd);
    RUN_TEST(test_verified_file_context_get_fd_null_path);
    RUN_TEST(test_verified_file_context_get_fd_path_mismatch);
    RUN_TEST(test_verified_file_context_copies_data);

    return UNITY_END();
}
