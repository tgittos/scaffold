#include "../../test/unity/unity.h"
#include "../../src/tools/subagent_tool.h"
#include "../../src/utils/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void setUp(void) {
    // Initialize config for each test
    config_init();
}

void tearDown(void) {
    // Clean up config after each test
    config_cleanup();
}

void test_subagent_manager_init_defaults(void) {
    SubagentManager manager;
    memset(&manager, 0xFF, sizeof(manager));  // Fill with garbage

    int result = subagent_manager_init(&manager);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NULL(manager.subagents);
    TEST_ASSERT_EQUAL_INT(0, manager.count);
    TEST_ASSERT_EQUAL_INT(5, manager.max_subagents);  // Default
    TEST_ASSERT_EQUAL_INT(300, manager.timeout_seconds);  // Default
    TEST_ASSERT_EQUAL_INT(0, manager.is_subagent_process);

    subagent_manager_cleanup(&manager);
}

void test_subagent_manager_init_with_config(void) {
    SubagentManager manager;

    int result = subagent_manager_init_with_config(&manager, 10, 600);

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NULL(manager.subagents);
    TEST_ASSERT_EQUAL_INT(0, manager.count);
    TEST_ASSERT_EQUAL_INT(10, manager.max_subagents);
    TEST_ASSERT_EQUAL_INT(600, manager.timeout_seconds);
    TEST_ASSERT_EQUAL_INT(0, manager.is_subagent_process);

    subagent_manager_cleanup(&manager);
}

void test_subagent_manager_init_null_pointer(void) {
    int result = subagent_manager_init(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = subagent_manager_init_with_config(NULL, 5, 300);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_subagent_manager_init_clamps_values(void) {
    SubagentManager manager;

    // Test clamping max_subagents (too low)
    int result = subagent_manager_init_with_config(&manager, 0, 300);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(5, manager.max_subagents);  // Should be clamped to default
    subagent_manager_cleanup(&manager);

    // Test clamping max_subagents (too high)
    result = subagent_manager_init_with_config(&manager, 100, 300);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(20, manager.max_subagents);  // Should be clamped to 20
    subagent_manager_cleanup(&manager);

    // Test clamping timeout (too low)
    result = subagent_manager_init_with_config(&manager, 5, 0);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(300, manager.timeout_seconds);  // Should be clamped to default
    subagent_manager_cleanup(&manager);

    // Test clamping timeout (too high)
    result = subagent_manager_init_with_config(&manager, 5, 7200);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(3600, manager.timeout_seconds);  // Should be clamped to 1 hour
    subagent_manager_cleanup(&manager);
}

void test_subagent_manager_cleanup_null(void) {
    // Should not crash
    subagent_manager_cleanup(NULL);
}

void test_subagent_manager_cleanup_empty(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);

    // Should handle empty manager gracefully
    subagent_manager_cleanup(&manager);

    TEST_ASSERT_NULL(manager.subagents);
    TEST_ASSERT_EQUAL_INT(0, manager.count);
}

void test_generate_subagent_id(void) {
    char id1[SUBAGENT_ID_LENGTH + 1];
    char id2[SUBAGENT_ID_LENGTH + 1];

    generate_subagent_id(id1);
    generate_subagent_id(id2);

    // Check length
    TEST_ASSERT_EQUAL_INT(SUBAGENT_ID_LENGTH, strlen(id1));
    TEST_ASSERT_EQUAL_INT(SUBAGENT_ID_LENGTH, strlen(id2));

    // Check that IDs are hex characters only
    for (int i = 0; i < SUBAGENT_ID_LENGTH; i++) {
        TEST_ASSERT_TRUE((id1[i] >= '0' && id1[i] <= '9') ||
                         (id1[i] >= 'a' && id1[i] <= 'f'));
        TEST_ASSERT_TRUE((id2[i] >= '0' && id2[i] <= '9') ||
                         (id2[i] >= 'a' && id2[i] <= 'f'));
    }

    // Check that IDs are different (extremely unlikely to be the same)
    TEST_ASSERT_TRUE(strcmp(id1, id2) != 0);
}

void test_generate_subagent_id_uniqueness(void) {
    // Generate 100 IDs and check they're all unique
    char ids[100][SUBAGENT_ID_LENGTH + 1];

    for (int i = 0; i < 100; i++) {
        generate_subagent_id(ids[i]);
    }

    // Check all pairs are unique
    for (int i = 0; i < 100; i++) {
        for (int j = i + 1; j < 100; j++) {
            TEST_ASSERT_TRUE(strcmp(ids[i], ids[j]) != 0);
        }
    }
}

void test_subagent_status_to_string(void) {
    TEST_ASSERT_EQUAL_STRING("pending", subagent_status_to_string(SUBAGENT_STATUS_PENDING));
    TEST_ASSERT_EQUAL_STRING("running", subagent_status_to_string(SUBAGENT_STATUS_RUNNING));
    TEST_ASSERT_EQUAL_STRING("completed", subagent_status_to_string(SUBAGENT_STATUS_COMPLETED));
    TEST_ASSERT_EQUAL_STRING("failed", subagent_status_to_string(SUBAGENT_STATUS_FAILED));
    TEST_ASSERT_EQUAL_STRING("timeout", subagent_status_to_string(SUBAGENT_STATUS_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("unknown", subagent_status_to_string((SubagentStatus)99));
}

void test_cleanup_subagent_null(void) {
    // Should not crash
    cleanup_subagent(NULL);
}

void test_cleanup_subagent_empty(void) {
    Subagent sub;
    memset(&sub, 0, sizeof(sub));
    sub.stdout_pipe[0] = -1;
    sub.stdout_pipe[1] = -1;

    // Should handle empty subagent gracefully
    cleanup_subagent(&sub);

    TEST_ASSERT_NULL(sub.task);
    TEST_ASSERT_NULL(sub.context);
    TEST_ASSERT_NULL(sub.output);
    TEST_ASSERT_NULL(sub.result);
    TEST_ASSERT_NULL(sub.error);
}

void test_cleanup_subagent_with_data(void) {
    Subagent sub;
    memset(&sub, 0, sizeof(sub));
    sub.stdout_pipe[0] = -1;
    sub.stdout_pipe[1] = -1;

    // Allocate some data
    sub.task = strdup("test task");
    sub.context = strdup("test context");
    sub.output = strdup("test output");
    sub.output_len = strlen(sub.output);
    sub.result = strdup("test result");
    sub.error = strdup("test error");

    // Clean up
    cleanup_subagent(&sub);

    TEST_ASSERT_NULL(sub.task);
    TEST_ASSERT_NULL(sub.context);
    TEST_ASSERT_NULL(sub.output);
    TEST_ASSERT_NULL(sub.result);
    TEST_ASSERT_NULL(sub.error);
    TEST_ASSERT_EQUAL_INT(0, sub.output_len);
}

void test_subagent_find_by_id_empty(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);

    Subagent *found = subagent_find_by_id(&manager, "abc123");
    TEST_ASSERT_NULL(found);

    subagent_manager_cleanup(&manager);
}

void test_subagent_find_by_id_null_params(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);

    TEST_ASSERT_NULL(subagent_find_by_id(NULL, "abc123"));
    TEST_ASSERT_NULL(subagent_find_by_id(&manager, NULL));

    subagent_manager_cleanup(&manager);
}

void test_subagent_poll_all_empty(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);

    int changed = subagent_poll_all(&manager);
    TEST_ASSERT_EQUAL_INT(0, changed);

    subagent_manager_cleanup(&manager);
}

void test_subagent_poll_all_null(void) {
    int changed = subagent_poll_all(NULL);
    TEST_ASSERT_EQUAL_INT(-1, changed);
}

void test_read_subagent_output_nonblocking_null(void) {
    int result = read_subagent_output_nonblocking(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_read_subagent_output_null(void) {
    int result = read_subagent_output(NULL);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_read_subagent_output_invalid_pipe(void) {
    Subagent sub;
    memset(&sub, 0, sizeof(sub));
    sub.stdout_pipe[0] = -1;

    int result = read_subagent_output_nonblocking(&sub);
    TEST_ASSERT_EQUAL_INT(-1, result);

    result = read_subagent_output(&sub);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_read_subagent_output_from_pipe(void) {
    int pipefd[2];
    TEST_ASSERT_EQUAL_INT(0, pipe(pipefd));

    Subagent sub;
    memset(&sub, 0, sizeof(sub));
    sub.stdout_pipe[0] = pipefd[0];
    sub.stdout_pipe[1] = pipefd[1];

    // Write some data to the pipe
    const char *test_data = "Hello, subagent!";
    write(pipefd[1], test_data, strlen(test_data));
    close(pipefd[1]);  // Close write end to signal EOF

    // Read the data
    int result = read_subagent_output(&sub);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_NOT_NULL(sub.output);
    TEST_ASSERT_EQUAL_STRING(test_data, sub.output);
    TEST_ASSERT_EQUAL_INT(strlen(test_data), sub.output_len);

    cleanup_subagent(&sub);
}

int main(void) {
    UNITY_BEGIN();

    // Manager initialization tests
    RUN_TEST(test_subagent_manager_init_defaults);
    RUN_TEST(test_subagent_manager_init_with_config);
    RUN_TEST(test_subagent_manager_init_null_pointer);
    RUN_TEST(test_subagent_manager_init_clamps_values);

    // Manager cleanup tests
    RUN_TEST(test_subagent_manager_cleanup_null);
    RUN_TEST(test_subagent_manager_cleanup_empty);

    // ID generation tests
    RUN_TEST(test_generate_subagent_id);
    RUN_TEST(test_generate_subagent_id_uniqueness);

    // Status string tests
    RUN_TEST(test_subagent_status_to_string);

    // Single subagent cleanup tests
    RUN_TEST(test_cleanup_subagent_null);
    RUN_TEST(test_cleanup_subagent_empty);
    RUN_TEST(test_cleanup_subagent_with_data);

    // Find by ID tests
    RUN_TEST(test_subagent_find_by_id_empty);
    RUN_TEST(test_subagent_find_by_id_null_params);

    // Poll tests
    RUN_TEST(test_subagent_poll_all_empty);
    RUN_TEST(test_subagent_poll_all_null);

    // Output reading tests
    RUN_TEST(test_read_subagent_output_nonblocking_null);
    RUN_TEST(test_read_subagent_output_null);
    RUN_TEST(test_read_subagent_output_invalid_pipe);
    RUN_TEST(test_read_subagent_output_from_pipe);

    return UNITY_END();
}
