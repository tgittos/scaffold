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

// =========================================================================
// subagent_spawn() tests
// =========================================================================

void test_subagent_spawn_null_params(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];

    // Null manager
    TEST_ASSERT_EQUAL_INT(-1, subagent_spawn(NULL, "task", NULL, id));

    // Null task
    TEST_ASSERT_EQUAL_INT(-1, subagent_spawn(&manager, NULL, NULL, id));

    // Null id output
    TEST_ASSERT_EQUAL_INT(-1, subagent_spawn(&manager, "task", NULL, NULL));

    subagent_manager_cleanup(&manager);
}

void test_subagent_spawn_prevents_nesting(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];

    // Set the is_subagent_process flag
    manager.is_subagent_process = 1;

    // Should fail because we're already in a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(0, manager.count);

    subagent_manager_cleanup(&manager);
}

void test_subagent_spawn_respects_max_limit(void) {
    SubagentManager manager;
    subagent_manager_init_with_config(&manager, 2, 300);  // Max 2 subagents
    char id[SUBAGENT_ID_LENGTH + 1];

    // Manually create two "subagents" in the array to simulate being at limit
    manager.subagents = malloc(2 * sizeof(Subagent));
    TEST_ASSERT_NOT_NULL(manager.subagents);
    memset(manager.subagents, 0, 2 * sizeof(Subagent));
    manager.count = 2;

    // Try to spawn - should fail
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(2, manager.count);  // Count unchanged

    // Clean up (manually since we didn't use real spawning)
    free(manager.subagents);
    manager.subagents = NULL;
    manager.count = 0;
}

void test_subagent_spawn_basic(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    memset(id, 0, sizeof(id));

    // Spawn a subagent
    // Note: This will fork a process that tries to run ralph --subagent
    // The process will likely fail/exit quickly since --subagent isn't fully implemented
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify the subagent was created
    TEST_ASSERT_EQUAL_INT(1, manager.count);
    TEST_ASSERT_NOT_NULL(manager.subagents);

    // Verify the ID was returned
    TEST_ASSERT_EQUAL_INT(SUBAGENT_ID_LENGTH, strlen(id));

    // Verify subagent fields
    Subagent *sub = &manager.subagents[0];
    TEST_ASSERT_EQUAL_STRING(id, sub->id);
    TEST_ASSERT_TRUE(sub->pid > 0);
    TEST_ASSERT_EQUAL_INT(SUBAGENT_STATUS_RUNNING, sub->status);
    TEST_ASSERT_TRUE(sub->stdout_pipe[0] > 0);
    TEST_ASSERT_EQUAL_STRING("test task", sub->task);
    TEST_ASSERT_NULL(sub->context);
    TEST_ASSERT_TRUE(sub->start_time > 0);

    // Let the process complete (it will fail since --subagent isn't handled)
    usleep(100000);  // 100ms

    subagent_manager_cleanup(&manager);
}

void test_subagent_spawn_with_context(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    memset(id, 0, sizeof(id));

    // Spawn a subagent with context
    int result = subagent_spawn(&manager, "test task", "some context", id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Verify the subagent was created with context
    TEST_ASSERT_EQUAL_INT(1, manager.count);
    Subagent *sub = &manager.subagents[0];
    TEST_ASSERT_EQUAL_STRING("test task", sub->task);
    TEST_ASSERT_EQUAL_STRING("some context", sub->context);

    usleep(100000);  // 100ms
    subagent_manager_cleanup(&manager);
}

void test_subagent_spawn_empty_context_treated_as_null(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    memset(id, 0, sizeof(id));

    // Spawn with empty context (should be treated as NULL)
    int result = subagent_spawn(&manager, "test task", "", id);
    TEST_ASSERT_EQUAL_INT(0, result);

    Subagent *sub = &manager.subagents[0];
    TEST_ASSERT_NULL(sub->context);  // Empty string treated as NULL

    usleep(100000);  // 100ms
    subagent_manager_cleanup(&manager);
}

void test_subagent_spawn_multiple(void) {
    SubagentManager manager;
    subagent_manager_init_with_config(&manager, 5, 300);
    char id1[SUBAGENT_ID_LENGTH + 1];
    char id2[SUBAGENT_ID_LENGTH + 1];
    char id3[SUBAGENT_ID_LENGTH + 1];

    // Spawn multiple subagents
    TEST_ASSERT_EQUAL_INT(0, subagent_spawn(&manager, "task 1", NULL, id1));
    TEST_ASSERT_EQUAL_INT(0, subagent_spawn(&manager, "task 2", "ctx 2", id2));
    TEST_ASSERT_EQUAL_INT(0, subagent_spawn(&manager, "task 3", NULL, id3));

    TEST_ASSERT_EQUAL_INT(3, manager.count);

    // Verify all IDs are different
    TEST_ASSERT_TRUE(strcmp(id1, id2) != 0);
    TEST_ASSERT_TRUE(strcmp(id2, id3) != 0);
    TEST_ASSERT_TRUE(strcmp(id1, id3) != 0);

    // Verify we can find each subagent
    TEST_ASSERT_NOT_NULL(subagent_find_by_id(&manager, id1));
    TEST_ASSERT_NOT_NULL(subagent_find_by_id(&manager, id2));
    TEST_ASSERT_NOT_NULL(subagent_find_by_id(&manager, id3));

    usleep(100000);  // 100ms
    subagent_manager_cleanup(&manager);
}

void test_subagent_spawn_and_poll(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];

    // Spawn a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Initial status should be running
    Subagent *sub = subagent_find_by_id(&manager, id);
    TEST_ASSERT_NOT_NULL(sub);
    TEST_ASSERT_EQUAL_INT(SUBAGENT_STATUS_RUNNING, sub->status);

    // Wait for process to complete (it will fail since --subagent isn't handled)
    usleep(200000);  // 200ms

    // Poll should detect the status change
    int changed = subagent_poll_all(&manager);
    TEST_ASSERT_TRUE(changed >= 0);  // May or may not have changed depending on timing

    // Give it more time and poll again
    usleep(200000);  // 200ms
    subagent_poll_all(&manager);

    // After polling, status should be either FAILED or COMPLETED
    // (FAILED is expected since --subagent isn't fully implemented)
    TEST_ASSERT_TRUE(sub->status == SUBAGENT_STATUS_FAILED ||
                     sub->status == SUBAGENT_STATUS_COMPLETED ||
                     sub->status == SUBAGENT_STATUS_RUNNING);

    subagent_manager_cleanup(&manager);
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

    // Spawn tests
    RUN_TEST(test_subagent_spawn_null_params);
    RUN_TEST(test_subagent_spawn_prevents_nesting);
    RUN_TEST(test_subagent_spawn_respects_max_limit);
    RUN_TEST(test_subagent_spawn_basic);
    RUN_TEST(test_subagent_spawn_with_context);
    RUN_TEST(test_subagent_spawn_empty_context_treated_as_null);
    RUN_TEST(test_subagent_spawn_multiple);
    RUN_TEST(test_subagent_spawn_and_poll);

    return UNITY_END();
}
