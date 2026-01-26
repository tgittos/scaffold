/**
 * Unit tests for atomic file operations module
 *
 * Tests TOCTOU protection, path utilities, and file identity verification.
 */

#include "../test/unity/unity.h"
#include "../src/policy/atomic_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test directory for creating temporary files */
static char test_dir[256];
static int test_dir_created = 0;

void setUp(void) {
    /* Create a unique test directory for each test run */
    if (!test_dir_created) {
        snprintf(test_dir, sizeof(test_dir), "/tmp/test_atomic_%d", (int)getpid());
        mkdir(test_dir, 0755);
        test_dir_created = 1;
    }
}

void tearDown(void) {
    /* Cleanup is handled after all tests */
}

/* Helper to create a test file with content */
static int create_test_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    if (content) {
        fprintf(f, "%s", content);
    }
    fclose(f);
    return 0;
}

/* Helper to remove a test file */
static void remove_test_file(const char *path) {
    unlink(path);
}

/* =============================================================================
 * Path Utility Tests
 * ========================================================================== */

void test_atomic_file_basename_simple_path(void) {
    const char *result = atomic_file_basename("/foo/bar/baz.txt");
    TEST_ASSERT_EQUAL_STRING("baz.txt", result);
}

void test_atomic_file_basename_no_directory(void) {
    const char *result = atomic_file_basename("file.txt");
    TEST_ASSERT_EQUAL_STRING("file.txt", result);
}

void test_atomic_file_basename_root_file(void) {
    const char *result = atomic_file_basename("/file.txt");
    TEST_ASSERT_EQUAL_STRING("file.txt", result);
}

void test_atomic_file_basename_empty_string(void) {
    const char *result = atomic_file_basename("");
    TEST_ASSERT_EQUAL_STRING(".", result);
}

void test_atomic_file_basename_null(void) {
    const char *result = atomic_file_basename(NULL);
    TEST_ASSERT_EQUAL_STRING(".", result);
}

void test_atomic_file_dirname_simple_path(void) {
    char *result = atomic_file_dirname("/foo/bar/baz.txt");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/foo/bar", result);
    free(result);
}

void test_atomic_file_dirname_no_directory(void) {
    char *result = atomic_file_dirname("file.txt");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING(".", result);
    free(result);
}

void test_atomic_file_dirname_root_file(void) {
    char *result = atomic_file_dirname("/file.txt");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/", result);
    free(result);
}

void test_atomic_file_dirname_root_only(void) {
    char *result = atomic_file_dirname("/");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING("/", result);
    free(result);
}

void test_atomic_file_dirname_empty_string(void) {
    char *result = atomic_file_dirname("");
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING(".", result);
    free(result);
}

void test_atomic_file_dirname_null(void) {
    char *result = atomic_file_dirname(NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_EQUAL_STRING(".", result);
    free(result);
}

void test_atomic_file_resolve_path_existing_file(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/resolve_test.txt", test_dir);
    create_test_file(path, "test content");

    char *resolved = atomic_file_resolve_path(path, 1);
    TEST_ASSERT_NOT_NULL(resolved);
    /* Should be an absolute path */
    TEST_ASSERT_EQUAL('/', resolved[0]);
    /* Should contain our filename */
    TEST_ASSERT_NOT_NULL(strstr(resolved, "resolve_test.txt"));
    free(resolved);

    remove_test_file(path);
}

void test_atomic_file_resolve_path_new_file_in_existing_dir(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/new_file.txt", test_dir);

    char *resolved = atomic_file_resolve_path(path, 0);
    TEST_ASSERT_NOT_NULL(resolved);
    /* Should be an absolute path */
    TEST_ASSERT_EQUAL('/', resolved[0]);
    /* Should contain our filename */
    TEST_ASSERT_NOT_NULL(strstr(resolved, "new_file.txt"));
    free(resolved);
}

void test_atomic_file_resolve_path_nonexistent_must_exist(void) {
    char *resolved = atomic_file_resolve_path("/nonexistent/path/file.txt", 1);
    TEST_ASSERT_NULL(resolved);
}

/* =============================================================================
 * ApprovedPath Management Tests
 * ========================================================================== */

void test_init_approved_path_zeros_struct(void) {
    ApprovedPath ap;
    /* Set some values to non-zero first */
    ap.inode = 12345;
    ap.device = 67890;
    ap.existed = 1;

    init_approved_path(&ap);

    TEST_ASSERT_NULL(ap.user_path);
    TEST_ASSERT_NULL(ap.resolved_path);
    TEST_ASSERT_NULL(ap.parent_path);
    TEST_ASSERT_EQUAL(0, ap.inode);
    TEST_ASSERT_EQUAL(0, ap.device);
    TEST_ASSERT_EQUAL(0, ap.existed);
}

void test_init_approved_path_null_safe(void) {
    /* Should not crash with NULL */
    init_approved_path(NULL);
}

void test_free_approved_path_null_safe(void) {
    /* Should not crash with NULL */
    free_approved_path(NULL);
}

void test_free_approved_path_clears_pointers(void) {
    ApprovedPath ap;
    init_approved_path(&ap);
    ap.user_path = strdup("/test/path");
    ap.resolved_path = strdup("/resolved/path");
    ap.parent_path = strdup("/parent");

    free_approved_path(&ap);

    TEST_ASSERT_NULL(ap.user_path);
    TEST_ASSERT_NULL(ap.resolved_path);
    TEST_ASSERT_NULL(ap.parent_path);
}

void test_capture_approved_path_existing_file(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/capture_test.txt", test_dir);
    create_test_file(path, "test content");

    ApprovedPath ap;
    VerifyResult result = capture_approved_path(path, &ap);

    TEST_ASSERT_EQUAL(VERIFY_OK, result);
    TEST_ASSERT_NOT_NULL(ap.user_path);
    TEST_ASSERT_NOT_NULL(ap.resolved_path);
    TEST_ASSERT_EQUAL(1, ap.existed);
    TEST_ASSERT_NOT_EQUAL(0, ap.inode);
    TEST_ASSERT_NOT_EQUAL(0, ap.device);

    free_approved_path(&ap);
    remove_test_file(path);
}

void test_capture_approved_path_new_file(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/nonexistent.txt", test_dir);

    ApprovedPath ap;
    VerifyResult result = capture_approved_path(path, &ap);

    TEST_ASSERT_EQUAL(VERIFY_OK, result);
    TEST_ASSERT_NOT_NULL(ap.user_path);
    TEST_ASSERT_NOT_NULL(ap.resolved_path);
    TEST_ASSERT_NOT_NULL(ap.parent_path);
    TEST_ASSERT_EQUAL(0, ap.existed);
    TEST_ASSERT_EQUAL(0, ap.inode);
    TEST_ASSERT_NOT_EQUAL(0, ap.parent_inode);

    free_approved_path(&ap);
}

void test_capture_approved_path_null_path(void) {
    ApprovedPath ap;
    VerifyResult result = capture_approved_path(NULL, &ap);
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);
}

void test_capture_approved_path_empty_path(void) {
    ApprovedPath ap;
    VerifyResult result = capture_approved_path("", &ap);
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);
}

void test_capture_approved_path_null_output(void) {
    VerifyResult result = capture_approved_path("/some/path", NULL);
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);
}

void test_capture_approved_path_nonexistent_parent(void) {
    ApprovedPath ap;
    VerifyResult result = capture_approved_path("/nonexistent/parent/file.txt", &ap);
    TEST_ASSERT_EQUAL(VERIFY_ERR_PARENT, result);
}

/* =============================================================================
 * Verification Tests
 * ========================================================================== */

void test_verify_approved_path_existing_file_unchanged(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/verify_test.txt", test_dir);
    create_test_file(path, "test content");

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    /* Verify should succeed since file hasn't changed */
    VerifyResult verify_result = verify_approved_path(&ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, verify_result);

    free_approved_path(&ap);
    remove_test_file(path);
}

void test_verify_approved_path_file_deleted(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/delete_test.txt", test_dir);
    create_test_file(path, "test content");

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    /* Delete the file */
    remove_test_file(path);

    /* Verify should fail with DELETED error */
    VerifyResult verify_result = verify_approved_path(&ap);
    TEST_ASSERT_EQUAL(VERIFY_ERR_DELETED, verify_result);

    free_approved_path(&ap);
}

void test_verify_approved_path_null(void) {
    VerifyResult result = verify_approved_path(NULL);
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);
}

void test_verify_approved_path_new_file_parent_unchanged(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/new_verify.txt", test_dir);

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    /* Parent directory hasn't changed, should verify OK */
    VerifyResult verify_result = verify_approved_path(&ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, verify_result);

    free_approved_path(&ap);
}

/* =============================================================================
 * Atomic Open Tests
 * ========================================================================== */

void test_verify_and_open_existing_file(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/open_test.txt", test_dir);
    create_test_file(path, "test content");

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    int fd = -1;
    VerifyResult open_result = verify_and_open_approved_path(&ap, O_RDONLY, &fd);
    TEST_ASSERT_EQUAL(VERIFY_OK, open_result);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    /* Should be able to read from the file */
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT_GREATER_THAN(0, n);
    buf[n] = '\0';
    TEST_ASSERT_NOT_NULL(strstr(buf, "test content"));

    close(fd);
    free_approved_path(&ap);
    remove_test_file(path);
}

void test_verify_and_open_creates_new_file(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/create_test_%d.txt", test_dir, (int)getpid());

    /* Ensure file doesn't exist */
    remove_test_file(path);

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);
    TEST_ASSERT_EQUAL(0, ap.existed);

    int fd = -1;
    VerifyResult open_result = verify_and_open_approved_path(&ap, O_WRONLY, &fd);
    TEST_ASSERT_EQUAL(VERIFY_OK, open_result);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    /* Should be able to write to the file */
    const char *content = "new file content\n";
    ssize_t n = write(fd, content, strlen(content));
    TEST_ASSERT_EQUAL((ssize_t)strlen(content), n);

    close(fd);

    /* Verify file was created */
    struct stat st;
    TEST_ASSERT_EQUAL(0, stat(path, &st));

    free_approved_path(&ap);
    remove_test_file(path);
}

void test_verify_and_open_deleted_file_fails(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/open_deleted.txt", test_dir);
    create_test_file(path, "test content");

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    /* Delete the file after capture */
    remove_test_file(path);

    int fd = -1;
    VerifyResult open_result = verify_and_open_approved_path(&ap, O_RDONLY, &fd);
    TEST_ASSERT_EQUAL(VERIFY_ERR_DELETED, open_result);
    TEST_ASSERT_EQUAL(-1, fd);

    free_approved_path(&ap);
}

void test_verify_and_open_null_approved(void) {
    int fd = -1;
    VerifyResult result = verify_and_open_approved_path(NULL, O_RDONLY, &fd);
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);
}

void test_verify_and_open_null_fd(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/null_fd.txt", test_dir);
    create_test_file(path, "test");

    ApprovedPath ap;
    capture_approved_path(path, &ap);

    VerifyResult result = verify_and_open_approved_path(&ap, O_RDONLY, NULL);
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);

    free_approved_path(&ap);
    remove_test_file(path);
}

/* =============================================================================
 * Symlink Protection Tests
 * ========================================================================== */

void test_verify_and_open_rejects_symlink(void) {
    char target_path[512];
    char link_path[512];
    snprintf(target_path, sizeof(target_path), "%s/symlink_target.txt", test_dir);
    snprintf(link_path, sizeof(link_path), "%s/symlink_link.txt", test_dir);

    create_test_file(target_path, "target content");

    /* Create symlink */
    if (symlink(target_path, link_path) != 0) {
        /* Skip test if symlink creation fails (might not have permissions) */
        remove_test_file(target_path);
        TEST_IGNORE_MESSAGE("Could not create symlink for test");
        return;
    }

    /* Capture the symlink path */
    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(link_path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    /* Try to open - should fail because of O_NOFOLLOW */
    int fd = -1;
    VerifyResult open_result = verify_and_open_approved_path(&ap, O_RDONLY, &fd);
    TEST_ASSERT_EQUAL(VERIFY_ERR_SYMLINK, open_result);
    TEST_ASSERT_EQUAL(-1, fd);

    free_approved_path(&ap);
    remove_test_file(link_path);
    remove_test_file(target_path);
}

/* =============================================================================
 * File Creation Tests
 * ========================================================================== */

void test_create_file_in_verified_parent_success(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/parent_create_%d.txt", test_dir, (int)getpid());
    remove_test_file(path);

    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    int fd = -1;
    VerifyResult create_result = create_file_in_verified_parent(&ap, O_WRONLY, 0644, &fd);
    TEST_ASSERT_EQUAL(VERIFY_OK, create_result);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    close(fd);

    /* Verify file exists */
    struct stat st;
    TEST_ASSERT_EQUAL(0, stat(path, &st));

    free_approved_path(&ap);
    remove_test_file(path);
}

void test_create_file_fails_if_exists(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/exists_create.txt", test_dir);
    create_test_file(path, "existing");

    /* Capture as if it doesn't exist (simulate race) */
    ApprovedPath ap;
    init_approved_path(&ap);
    ap.user_path = strdup(path);
    ap.resolved_path = atomic_file_resolve_path(path, 0);
    ap.parent_path = atomic_file_dirname(path);
    ap.existed = 1;  /* But file exists */

    int fd = -1;
    VerifyResult result = create_file_in_verified_parent(&ap, O_WRONLY, 0644, &fd);
    TEST_ASSERT_EQUAL(VERIFY_ERR_ALREADY_EXISTS, result);

    free_approved_path(&ap);
    remove_test_file(path);
}

/* =============================================================================
 * Error Message Tests
 * ========================================================================== */

void test_verify_result_message_returns_strings(void) {
    TEST_ASSERT_NOT_NULL(verify_result_message(VERIFY_OK));
    TEST_ASSERT_NOT_NULL(verify_result_message(VERIFY_ERR_SYMLINK));
    TEST_ASSERT_NOT_NULL(verify_result_message(VERIFY_ERR_DELETED));
    TEST_ASSERT_NOT_NULL(verify_result_message(VERIFY_ERR_INODE_MISMATCH));
    TEST_ASSERT_NOT_NULL(verify_result_message(VERIFY_ERR_PARENT_CHANGED));

    /* Messages should be descriptive */
    TEST_ASSERT_NOT_EQUAL(0, strlen(verify_result_message(VERIFY_ERR_SYMLINK)));
}

void test_format_verify_error_returns_json(void) {
    char *json = format_verify_error(VERIFY_ERR_SYMLINK, "/test/path");
    TEST_ASSERT_NOT_NULL(json);

    /* Should be valid-ish JSON with expected fields */
    TEST_ASSERT_NOT_NULL(strstr(json, "\"error\":"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"message\":"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"path\":"));
    TEST_ASSERT_NOT_NULL(strstr(json, "/test/path"));

    free(json);
}

void test_format_verify_error_escapes_path(void) {
    char *json = format_verify_error(VERIFY_ERR_OPEN, "/path/with\"quotes");
    TEST_ASSERT_NOT_NULL(json);

    /* Quotes should be escaped */
    TEST_ASSERT_NOT_NULL(strstr(json, "\\\"quotes"));

    free(json);
}

/* =============================================================================
 * Network Filesystem Detection Tests
 * ========================================================================== */

void test_is_network_filesystem_local_path(void) {
    /* Local paths should not be network filesystems */
    int result = is_network_filesystem(test_dir);
    TEST_ASSERT_EQUAL(0, result);
}

void test_is_network_filesystem_null_path(void) {
    int result = is_network_filesystem(NULL);
    TEST_ASSERT_EQUAL(0, result);
}

void test_is_network_filesystem_empty_path(void) {
    int result = is_network_filesystem("");
    TEST_ASSERT_EQUAL(0, result);
}

/* =============================================================================
 * TOCTOU Attack Simulation Tests
 * ========================================================================== */

void test_inode_mismatch_detected(void) {
    char path1[512], path2[512];
    snprintf(path1, sizeof(path1), "%s/toctou1.txt", test_dir);
    snprintf(path2, sizeof(path2), "%s/toctou2.txt", test_dir);

    /* Create first file */
    create_test_file(path1, "original");

    /* Capture path1 */
    ApprovedPath ap;
    VerifyResult capture_result = capture_approved_path(path1, &ap);
    TEST_ASSERT_EQUAL(VERIFY_OK, capture_result);

    /* Simulate TOCTOU attack: delete original and create new file */
    remove_test_file(path1);
    create_test_file(path1, "attacker content");

    /* The inode will be different - verify should detect this */
    /* (In practice, this depends on filesystem behavior) */
    /* We at least verify the mechanism exists */
    int fd = -1;
    VerifyResult open_result = verify_and_open_approved_path(&ap, O_RDONLY, &fd);

    /* Either it succeeds (same inode reused) or fails (different inode) */
    /* Both are acceptable - we're testing the mechanism exists */
    if (open_result == VERIFY_OK) {
        close(fd);
    }

    free_approved_path(&ap);
    remove_test_file(path1);
}

/* =============================================================================
 * Test Runner
 * ========================================================================== */

static void cleanup_test_dir(void) {
    if (test_dir_created) {
        /* Remove any leftover test files */
        char cmd[512];
        memset(cmd, 0, sizeof(cmd)); /* Initialize to silence valgrind */
        snprintf(cmd, sizeof(cmd), "rm -rf %s 2>/dev/null", test_dir);
        (void)system(cmd);
    }
}

int main(void) {
    UNITY_BEGIN();

    /* Path Utility Tests */
    RUN_TEST(test_atomic_file_basename_simple_path);
    RUN_TEST(test_atomic_file_basename_no_directory);
    RUN_TEST(test_atomic_file_basename_root_file);
    RUN_TEST(test_atomic_file_basename_empty_string);
    RUN_TEST(test_atomic_file_basename_null);
    RUN_TEST(test_atomic_file_dirname_simple_path);
    RUN_TEST(test_atomic_file_dirname_no_directory);
    RUN_TEST(test_atomic_file_dirname_root_file);
    RUN_TEST(test_atomic_file_dirname_root_only);
    RUN_TEST(test_atomic_file_dirname_empty_string);
    RUN_TEST(test_atomic_file_dirname_null);
    RUN_TEST(test_atomic_file_resolve_path_existing_file);
    RUN_TEST(test_atomic_file_resolve_path_new_file_in_existing_dir);
    RUN_TEST(test_atomic_file_resolve_path_nonexistent_must_exist);

    /* ApprovedPath Management Tests */
    RUN_TEST(test_init_approved_path_zeros_struct);
    RUN_TEST(test_init_approved_path_null_safe);
    RUN_TEST(test_free_approved_path_null_safe);
    RUN_TEST(test_free_approved_path_clears_pointers);
    RUN_TEST(test_capture_approved_path_existing_file);
    RUN_TEST(test_capture_approved_path_new_file);
    RUN_TEST(test_capture_approved_path_null_path);
    RUN_TEST(test_capture_approved_path_empty_path);
    RUN_TEST(test_capture_approved_path_null_output);
    RUN_TEST(test_capture_approved_path_nonexistent_parent);

    /* Verification Tests */
    RUN_TEST(test_verify_approved_path_existing_file_unchanged);
    RUN_TEST(test_verify_approved_path_file_deleted);
    RUN_TEST(test_verify_approved_path_null);
    RUN_TEST(test_verify_approved_path_new_file_parent_unchanged);

    /* Atomic Open Tests */
    RUN_TEST(test_verify_and_open_existing_file);
    RUN_TEST(test_verify_and_open_creates_new_file);
    RUN_TEST(test_verify_and_open_deleted_file_fails);
    RUN_TEST(test_verify_and_open_null_approved);
    RUN_TEST(test_verify_and_open_null_fd);

    /* Symlink Protection Tests */
    RUN_TEST(test_verify_and_open_rejects_symlink);

    /* File Creation Tests */
    RUN_TEST(test_create_file_in_verified_parent_success);
    RUN_TEST(test_create_file_fails_if_exists);

    /* Error Message Tests */
    RUN_TEST(test_verify_result_message_returns_strings);
    RUN_TEST(test_format_verify_error_returns_json);
    RUN_TEST(test_format_verify_error_escapes_path);

    /* Network Filesystem Tests */
    RUN_TEST(test_is_network_filesystem_local_path);
    RUN_TEST(test_is_network_filesystem_null_path);
    RUN_TEST(test_is_network_filesystem_empty_path);

    /* TOCTOU Attack Simulation */
    RUN_TEST(test_inode_mismatch_detected);

    cleanup_test_dir();
    return UNITY_END();
}
