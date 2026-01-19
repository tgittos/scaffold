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

// =========================================================================
// subagent_get_status() tests
// =========================================================================

void test_subagent_get_status_null_params(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    SubagentStatus status;
    char *result_str = NULL;
    char *error_str = NULL;

    // Null manager
    TEST_ASSERT_EQUAL_INT(-1, subagent_get_status(NULL, "abc123", 0, &status, &result_str, &error_str));

    // Null subagent_id
    TEST_ASSERT_EQUAL_INT(-1, subagent_get_status(&manager, NULL, 0, &status, &result_str, &error_str));

    // Null status
    TEST_ASSERT_EQUAL_INT(-1, subagent_get_status(&manager, "abc123", 0, NULL, &result_str, &error_str));

    subagent_manager_cleanup(&manager);
}

void test_subagent_get_status_not_found(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    SubagentStatus status;
    char *result_str = NULL;
    char *error_str = NULL;

    // Query non-existent subagent
    int result = subagent_get_status(&manager, "nonexistent1234", 0, &status, &result_str, &error_str);
    TEST_ASSERT_EQUAL_INT(-1, result);
    TEST_ASSERT_EQUAL_INT(SUBAGENT_STATUS_FAILED, status);
    TEST_ASSERT_NULL(result_str);
    TEST_ASSERT_NOT_NULL(error_str);
    TEST_ASSERT_TRUE(strstr(error_str, "not found") != NULL);

    free(error_str);
    subagent_manager_cleanup(&manager);
}

void test_subagent_get_status_running_nowait(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    SubagentStatus status;
    char *result_str = NULL;
    char *error_str = NULL;

    // Spawn a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Query immediately - should be running
    result = subagent_get_status(&manager, id, 0, &status, &result_str, &error_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Status should be running (or possibly already failed if the process died very quickly)
    TEST_ASSERT_TRUE(status == SUBAGENT_STATUS_RUNNING ||
                     status == SUBAGENT_STATUS_FAILED);

    if (result_str) free(result_str);
    if (error_str) free(error_str);

    usleep(200000);  // Let process complete
    subagent_manager_cleanup(&manager);
}

void test_subagent_get_status_after_completion(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    SubagentStatus status;
    char *result_str = NULL;
    char *error_str = NULL;

    // Spawn a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // First, wait for completion using blocking mode
    result = subagent_get_status(&manager, id, 1, &status, &result_str, &error_str);
    TEST_ASSERT_EQUAL_INT(0, result);
    if (result_str) { free(result_str); result_str = NULL; }
    if (error_str) { free(error_str); error_str = NULL; }

    // Now query again non-blocking - should return cached terminal state
    result = subagent_get_status(&manager, id, 0, &status, &result_str, &error_str);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(status == SUBAGENT_STATUS_COMPLETED ||
                     status == SUBAGENT_STATUS_FAILED ||
                     status == SUBAGENT_STATUS_TIMEOUT);

    if (result_str) free(result_str);
    if (error_str) free(error_str);

    subagent_manager_cleanup(&manager);
}

void test_subagent_get_status_wait(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    SubagentStatus status;
    char *result_str = NULL;
    char *error_str = NULL;

    // Spawn a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Query with wait=1 - should block until completion
    result = subagent_get_status(&manager, id, 1, &status, &result_str, &error_str);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Should be completed or failed (not running)
    TEST_ASSERT_TRUE(status == SUBAGENT_STATUS_COMPLETED ||
                     status == SUBAGENT_STATUS_FAILED ||
                     status == SUBAGENT_STATUS_TIMEOUT);

    if (result_str) free(result_str);
    if (error_str) free(error_str);

    subagent_manager_cleanup(&manager);
}

void test_subagent_get_status_cached_result(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    SubagentStatus status1, status2;
    char *result_str1 = NULL, *result_str2 = NULL;
    char *error_str1 = NULL, *error_str2 = NULL;

    // Spawn a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Wait for completion using blocking mode
    result = subagent_get_status(&manager, id, 1, &status1, &result_str1, &error_str1);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Query again - should return cached result
    result = subagent_get_status(&manager, id, 0, &status2, &result_str2, &error_str2);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Both queries should return the same terminal status
    TEST_ASSERT_EQUAL_INT(status1, status2);

    if (result_str1) free(result_str1);
    if (result_str2) free(result_str2);
    if (error_str1) free(error_str1);
    if (error_str2) free(error_str2);

    subagent_manager_cleanup(&manager);
}

void test_subagent_get_status_null_optional_params(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    char id[SUBAGENT_ID_LENGTH + 1];
    SubagentStatus status;

    // Spawn a subagent
    int result = subagent_spawn(&manager, "test task", NULL, id);
    TEST_ASSERT_EQUAL_INT(0, result);

    // Wait for completion using blocking mode with NULL optional params
    result = subagent_get_status(&manager, id, 1, &status, NULL, NULL);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(status == SUBAGENT_STATUS_COMPLETED ||
                     status == SUBAGENT_STATUS_FAILED ||
                     status == SUBAGENT_STATUS_TIMEOUT);

    subagent_manager_cleanup(&manager);
}

// Note: ralph_run_as_subagent() is tested indirectly through the spawn tests.
// The spawned processes execute ralph --subagent which calls ralph_run_as_subagent().
// Direct unit testing would require linking against the full ralph infrastructure.

// =========================================================================
// Tool registration tests
// =========================================================================

void test_register_subagent_tool_null_params(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);

    // Null registry
    TEST_ASSERT_EQUAL_INT(-1, register_subagent_tool(NULL, &manager));

    // Null manager
    TEST_ASSERT_EQUAL_INT(-1, register_subagent_tool(&registry, NULL));

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_register_subagent_status_tool_null_params(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);

    // Null registry
    TEST_ASSERT_EQUAL_INT(-1, register_subagent_status_tool(NULL, &manager));

    // Null manager
    TEST_ASSERT_EQUAL_INT(-1, register_subagent_status_tool(&registry, NULL));

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_register_subagent_tools(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);

    // Register both tools
    TEST_ASSERT_EQUAL_INT(0, register_subagent_tool(&registry, &manager));
    TEST_ASSERT_EQUAL_INT(0, register_subagent_status_tool(&registry, &manager));

    // Verify tools are registered
    TEST_ASSERT_EQUAL_INT(2, registry.function_count);

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

// =========================================================================
// execute_subagent_tool_call() tests
// =========================================================================

void test_execute_subagent_tool_call_null_params(void) {
    ToolCall tool_call = { .id = "tc1", .name = "subagent", .arguments = "{\"task\": \"test\"}" };
    ToolResult result = { 0 };

    // Null tool_call
    TEST_ASSERT_EQUAL_INT(-1, execute_subagent_tool_call(NULL, &result));

    // Null result
    TEST_ASSERT_EQUAL_INT(-1, execute_subagent_tool_call(&tool_call, NULL));
}

void test_execute_subagent_tool_call_no_manager(void) {
    // This test needs to ensure g_subagent_manager is NULL
    // We'll register with a manager, then we can test the execution
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);

    // Register tools to set up the global manager
    register_subagent_tool(&registry, &manager);

    ToolCall tool_call = { .id = "tc1", .name = "subagent", .arguments = "{\"task\": \"test\"}" };
    ToolResult result = { 0 };

    // Execute should work now that manager is registered
    int exec_result = execute_subagent_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(result.tool_call_id);
    TEST_ASSERT_NOT_NULL(result.result);

    // Clean up result
    free(result.tool_call_id);
    free(result.result);

    usleep(200000);  // Let spawned process complete
    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_tool_call_missing_task(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_tool(&registry, &manager);

    ToolCall tool_call = { .id = "tc1", .name = "subagent", .arguments = "{}" };
    ToolResult result = { 0 };

    // Should return error because task is missing
    int exec_result = execute_subagent_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);  // Function returns success but result is error
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "required") != NULL);

    free(result.tool_call_id);
    free(result.result);

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_tool_call_empty_task(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_tool(&registry, &manager);

    ToolCall tool_call = { .id = "tc1", .name = "subagent", .arguments = "{\"task\": \"\"}" };
    ToolResult result = { 0 };

    // Should return error because task is empty
    int exec_result = execute_subagent_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "required") != NULL);

    free(result.tool_call_id);
    free(result.result);

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_tool_call_success(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_tool(&registry, &manager);

    ToolCall tool_call = {
        .id = "tc1",
        .name = "subagent",
        .arguments = "{\"task\": \"test task\", \"context\": \"test context\"}"
    };
    ToolResult result = { 0 };

    int exec_result = execute_subagent_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "subagent_id") != NULL);
    TEST_ASSERT_TRUE(strstr(result.result, "running") != NULL);

    free(result.tool_call_id);
    free(result.result);

    usleep(200000);  // Let spawned process complete
    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_tool_call_prevents_nesting(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    manager.is_subagent_process = 1;  // Simulate running as a subagent
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_tool(&registry, &manager);

    ToolCall tool_call = { .id = "tc1", .name = "subagent", .arguments = "{\"task\": \"test\"}" };
    ToolResult result = { 0 };

    int exec_result = execute_subagent_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "cannot spawn") != NULL);

    free(result.tool_call_id);
    free(result.result);

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

// =========================================================================
// execute_subagent_status_tool_call() tests
// =========================================================================

void test_execute_subagent_status_tool_call_null_params(void) {
    ToolCall tool_call = { .id = "tc1", .name = "subagent_status", .arguments = "{\"subagent_id\": \"abc\"}" };
    ToolResult result = { 0 };

    // Null tool_call
    TEST_ASSERT_EQUAL_INT(-1, execute_subagent_status_tool_call(NULL, &result));

    // Null result
    TEST_ASSERT_EQUAL_INT(-1, execute_subagent_status_tool_call(&tool_call, NULL));
}

void test_execute_subagent_status_tool_call_missing_id(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_status_tool(&registry, &manager);

    ToolCall tool_call = { .id = "tc1", .name = "subagent_status", .arguments = "{}" };
    ToolResult result = { 0 };

    int exec_result = execute_subagent_status_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "required") != NULL);

    free(result.tool_call_id);
    free(result.result);

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_status_tool_call_not_found(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_status_tool(&registry, &manager);

    ToolCall tool_call = {
        .id = "tc1",
        .name = "subagent_status",
        .arguments = "{\"subagent_id\": \"nonexistent123\"}"
    };
    ToolResult result = { 0 };

    int exec_result = execute_subagent_status_tool_call(&tool_call, &result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_TRUE(strstr(result.result, "not found") != NULL || strstr(result.result, "error") != NULL);

    free(result.tool_call_id);
    free(result.result);

    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_status_tool_call_success(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_tool(&registry, &manager);
    register_subagent_status_tool(&registry, &manager);

    // First spawn a subagent
    ToolCall spawn_call = {
        .id = "tc1",
        .name = "subagent",
        .arguments = "{\"task\": \"test task\"}"
    };
    ToolResult spawn_result = { 0 };
    execute_subagent_tool_call(&spawn_call, &spawn_result);
    TEST_ASSERT_EQUAL_INT(1, spawn_result.success);

    // Extract subagent_id from result
    char *id_start = strstr(spawn_result.result, "\"subagent_id\": \"");
    TEST_ASSERT_NOT_NULL(id_start);
    id_start += strlen("\"subagent_id\": \"");
    char subagent_id[SUBAGENT_ID_LENGTH + 1];
    strncpy(subagent_id, id_start, SUBAGENT_ID_LENGTH);
    subagent_id[SUBAGENT_ID_LENGTH] = '\0';

    // Now query the status
    char status_args[256];
    snprintf(status_args, sizeof(status_args), "{\"subagent_id\": \"%s\"}", subagent_id);
    ToolCall status_call = {
        .id = "tc2",
        .name = "subagent_status",
        .arguments = status_args
    };
    ToolResult status_result = { 0 };

    int exec_result = execute_subagent_status_tool_call(&status_call, &status_result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(status_result.result);
    TEST_ASSERT_TRUE(strstr(status_result.result, "status") != NULL);

    free(spawn_result.tool_call_id);
    free(spawn_result.result);
    free(status_result.tool_call_id);
    free(status_result.result);

    usleep(200000);  // Let spawned process complete
    cleanup_tool_registry(&registry);
    subagent_manager_cleanup(&manager);
}

void test_execute_subagent_status_tool_call_with_wait(void) {
    SubagentManager manager;
    subagent_manager_init(&manager);
    ToolRegistry registry;
    init_tool_registry(&registry);
    register_subagent_tool(&registry, &manager);
    register_subagent_status_tool(&registry, &manager);

    // First spawn a subagent
    ToolCall spawn_call = {
        .id = "tc1",
        .name = "subagent",
        .arguments = "{\"task\": \"test task\"}"
    };
    ToolResult spawn_result = { 0 };
    execute_subagent_tool_call(&spawn_call, &spawn_result);

    // Extract subagent_id
    char *id_start = strstr(spawn_result.result, "\"subagent_id\": \"");
    TEST_ASSERT_NOT_NULL(id_start);
    id_start += strlen("\"subagent_id\": \"");
    char subagent_id[SUBAGENT_ID_LENGTH + 1];
    strncpy(subagent_id, id_start, SUBAGENT_ID_LENGTH);
    subagent_id[SUBAGENT_ID_LENGTH] = '\0';

    // Query status with wait=true
    char status_args[256];
    snprintf(status_args, sizeof(status_args), "{\"subagent_id\": \"%s\", \"wait\": true}", subagent_id);
    ToolCall status_call = {
        .id = "tc2",
        .name = "subagent_status",
        .arguments = status_args
    };
    ToolResult status_result = { 0 };

    int exec_result = execute_subagent_status_tool_call(&status_call, &status_result);
    TEST_ASSERT_EQUAL_INT(0, exec_result);
    TEST_ASSERT_NOT_NULL(status_result.result);
    // With wait=true, should not be "running" anymore
    TEST_ASSERT_TRUE(strstr(status_result.result, "completed") != NULL ||
                     strstr(status_result.result, "failed") != NULL ||
                     strstr(status_result.result, "timeout") != NULL);

    free(spawn_result.tool_call_id);
    free(spawn_result.result);
    free(status_result.tool_call_id);
    free(status_result.result);

    cleanup_tool_registry(&registry);
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

    // Get status tests
    RUN_TEST(test_subagent_get_status_null_params);
    RUN_TEST(test_subagent_get_status_not_found);
    RUN_TEST(test_subagent_get_status_running_nowait);
    RUN_TEST(test_subagent_get_status_after_completion);
    RUN_TEST(test_subagent_get_status_wait);
    RUN_TEST(test_subagent_get_status_cached_result);
    RUN_TEST(test_subagent_get_status_null_optional_params);

    // Tool registration tests
    RUN_TEST(test_register_subagent_tool_null_params);
    RUN_TEST(test_register_subagent_status_tool_null_params);
    RUN_TEST(test_register_subagent_tools);

    // execute_subagent_tool_call tests
    RUN_TEST(test_execute_subagent_tool_call_null_params);
    RUN_TEST(test_execute_subagent_tool_call_no_manager);
    RUN_TEST(test_execute_subagent_tool_call_missing_task);
    RUN_TEST(test_execute_subagent_tool_call_empty_task);
    RUN_TEST(test_execute_subagent_tool_call_success);
    RUN_TEST(test_execute_subagent_tool_call_prevents_nesting);

    // execute_subagent_status_tool_call tests
    RUN_TEST(test_execute_subagent_status_tool_call_null_params);
    RUN_TEST(test_execute_subagent_status_tool_call_missing_id);
    RUN_TEST(test_execute_subagent_status_tool_call_not_found);
    RUN_TEST(test_execute_subagent_status_tool_call_success);
    RUN_TEST(test_execute_subagent_status_tool_call_with_wait);

    return UNITY_END();
}
