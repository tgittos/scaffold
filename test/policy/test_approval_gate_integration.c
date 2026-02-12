/**
 * Integration tests for approval gate system
 *
 * Tests end-to-end approval flows including:
 * - Non-interactive mode denial
 * - Allowlist matching bypass
 * - Rate limiting across multiple calls
 * - Batch approval mechanics
 * - Allow always pattern generation
 *
 * Note: Tests that require actual TTY prompting use pseudoterminal (pty)
 * for mock input where possible, or test the underlying logic directly.
 *
 * Dependencies: This test requires GATE_DEPS from mk/tests.mk which includes
 * all policy module sources and the subagent stub.
 */

#include "../test/unity/unity.h"
#include "policy/approval_gate.h"
#include "policy/protected_files.h"
#include "policy/rate_limiter.h"
#include "util/app_home.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Python tool stubs are provided by test/stubs/python_tool_stub.c */

/* Test fixture */
static ApprovalGateConfig config;

void setUp(void) {
    int home_result = app_home_init(NULL);
    TEST_ASSERT_EQUAL(0, home_result);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);
}

void tearDown(void) {
    approval_gate_cleanup(&config);
    app_home_cleanup();
}

/* =============================================================================
 * Non-Interactive Mode Tests
 * ========================================================================== */

void test_non_interactive_denies_gated_operations(void) {
    /* Set up non-interactive mode */
    config.is_interactive = 0;

    /* Create a gated tool call (shell command) */
    ToolCall tool_call = {0};
    tool_call.id = "call_001";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"ls -la\"}";

    ApprovedPath path = {0};

    /* Should be denied without prompting */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, result);

    free_approved_path(&path);
}

void test_non_interactive_allows_allowed_category(void) {
    /* Set up non-interactive mode */
    config.is_interactive = 0;

    /* Create a tool call in an allowed category (file_read) */
    ToolCall tool_call = {0};
    tool_call.id = "call_002";
    tool_call.name = "read_file";
    tool_call.arguments = "{\"path\": \"/tmp/test.txt\"}";

    ApprovedPath path = {0};

    /* Should be allowed (file_read category is ALLOW by default) */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);

    free_approved_path(&path);
}

void test_non_interactive_with_category_override(void) {
    /* Set up non-interactive mode with shell allowed */
    config.is_interactive = 0;
    config.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_ALLOW;

    /* Create a shell tool call */
    ToolCall tool_call = {0};
    tool_call.id = "call_003";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"echo hello\"}";

    ApprovedPath path = {0};

    /* Should be allowed due to category override */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);

    free_approved_path(&path);
}

/* =============================================================================
 * Allowlist Bypass Tests
 * ========================================================================== */

void test_allowlist_regex_bypasses_gate(void) {
    /* Add regex pattern to allowlist for write_file tool
     * Note: The regex matches against the full arguments JSON, not just the path */
    int add_result = approval_gate_add_allowlist(&config, "write_file", "/tmp/.*\\.txt");
    TEST_ASSERT_EQUAL(0, add_result);

    /* Create a matching tool call */
    ToolCall tool_call = {0};
    tool_call.id = "call_004";
    tool_call.name = "write_file";
    tool_call.arguments = "{\"path\": \"/tmp/test.txt\", \"content\": \"hello\"}";

    ApprovedPath path = {0};

    /* Should be allowed due to allowlist match */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);

    free_approved_path(&path);
}

void test_allowlist_non_matching_requires_gate(void) {
    /* Set up non-interactive to force denial on gate */
    config.is_interactive = 0;

    /* Add regex pattern to allowlist for write_file tool
     * Note: The regex matches against the full arguments JSON */
    int add_result = approval_gate_add_allowlist(&config, "write_file", "/tmp/.*\\.txt");
    TEST_ASSERT_EQUAL(0, add_result);

    /* Create a non-matching tool call (different extension) */
    ToolCall tool_call = {0};
    tool_call.id = "call_005";
    tool_call.name = "write_file";
    tool_call.arguments = "{\"path\": \"/tmp/test.json\", \"content\": \"{}\"}";

    ApprovedPath path = {0};

    /* Should be denied (no match, non-interactive) */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, result);

    free_approved_path(&path);
}

void test_shell_allowlist_prefix_matching(void) {
    /* Add shell command prefix to allowlist */
    const char *prefix[] = {"git", "status"};
    int add_result = approval_gate_add_shell_allowlist(&config, prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, add_result);

    /* Create a matching shell command */
    ToolCall tool_call = {0};
    tool_call.id = "call_006";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"git status -s\"}";

    ApprovedPath path = {0};

    /* Should be allowed due to prefix match */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);

    free_approved_path(&path);
}

void test_shell_allowlist_chain_blocked(void) {
    /* Set up non-interactive mode */
    config.is_interactive = 0;

    /* Add shell command prefix to allowlist */
    const char *prefix[] = {"echo", "hello"};
    int add_result = approval_gate_add_shell_allowlist(&config, prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, add_result);

    /* Create a command with chain operator (should NOT be allowed) */
    ToolCall tool_call = {0};
    tool_call.id = "call_007";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"echo hello; rm -rf /\"}";

    ApprovedPath path = {0};

    /* Should be denied due to chain operator */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, result);

    free_approved_path(&path);
}

/* =============================================================================
 * Rate Limiting Tests
 * ========================================================================== */

void test_rate_limiting_after_denials(void) {
    /* We need to track denials through the rate limiter */
    TEST_ASSERT_NOT_NULL(config.rate_limiter);

    /* Create a tool call */
    ToolCall tool_call = {0};
    tool_call.id = "call_008";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"dangerous_command\"}";

    /* Track multiple denials to trigger rate limiting (3+ for backoff) */
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);

    /* Should now be rate limited */
    int limited = is_rate_limited(&config, &tool_call);
    TEST_ASSERT_EQUAL(1, limited);

    /* Check rate limit remaining */
    int remaining = get_rate_limit_remaining(&config, "shell");
    TEST_ASSERT_GREATER_THAN(0, remaining);
}

void test_rate_limiting_reset_on_approval(void) {
    /* Track some denials */
    ToolCall tool_call = {0};
    tool_call.id = "call_009";
    tool_call.name = "test_tool";
    tool_call.arguments = "{}";

    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);

    /* Reset the tracker (simulating approval) */
    reset_denial_tracker(&config, "test_tool");

    /* Should no longer be rate limited */
    int limited = is_rate_limited(&config, &tool_call);
    TEST_ASSERT_EQUAL(0, limited);
}

void test_rate_limiting_different_tools_independent(void) {
    ToolCall tool_call_a = {0};
    tool_call_a.id = "call_010a";
    tool_call_a.name = "tool_a";
    tool_call_a.arguments = "{}";

    ToolCall tool_call_b = {0};
    tool_call_b.id = "call_010b";
    tool_call_b.name = "tool_b";
    tool_call_b.arguments = "{}";

    /* Track denials for tool_a */
    track_denial(&config, &tool_call_a);
    track_denial(&config, &tool_call_a);
    track_denial(&config, &tool_call_a);

    /* tool_a should be rate limited */
    TEST_ASSERT_EQUAL(1, is_rate_limited(&config, &tool_call_a));

    /* tool_b should not be rate limited */
    TEST_ASSERT_EQUAL(0, is_rate_limited(&config, &tool_call_b));
}

/* =============================================================================
 * Batch Approval Tests
 * ========================================================================== */

void test_batch_all_allowed_category(void) {
    /* Create multiple tool calls in allowed category */
    ToolCall tool_calls[3];
    memset(tool_calls, 0, sizeof(tool_calls));

    tool_calls[0].id = "call_batch_1";
    tool_calls[0].name = "read_file";
    tool_calls[0].arguments = "{\"path\": \"/tmp/a.txt\"}";

    tool_calls[1].id = "call_batch_2";
    tool_calls[1].name = "read_file";
    tool_calls[1].arguments = "{\"path\": \"/tmp/b.txt\"}";

    tool_calls[2].id = "call_batch_3";
    tool_calls[2].name = "read_file";
    tool_calls[2].arguments = "{\"path\": \"/tmp/c.txt\"}";

    ApprovalBatchResult batch = {0};

    /* All should be allowed (file_read category is ALLOW) */
    ApprovalResult result = check_approval_gate_batch(&config, tool_calls, 3, &batch);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);
    TEST_ASSERT_EQUAL(3, batch.count);

    /* Each result should be ALLOWED */
    for (int i = 0; i < batch.count; i++) {
        TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[i]);
    }

    free_batch_result(&batch);
}

void test_batch_mixed_categories_non_interactive(void) {
    /* Set up non-interactive mode */
    config.is_interactive = 0;

    /* Create mixed category tool calls */
    ToolCall tool_calls[3];
    memset(tool_calls, 0, sizeof(tool_calls));

    /* Allowed category (file_read) */
    tool_calls[0].id = "call_batch_4";
    tool_calls[0].name = "read_file";
    tool_calls[0].arguments = "{\"path\": \"/tmp/a.txt\"}";

    /* Gated category (shell) */
    tool_calls[1].id = "call_batch_5";
    tool_calls[1].name = "shell";
    tool_calls[1].arguments = "{\"command\": \"ls\"}";

    /* Allowed category (file_read) */
    tool_calls[2].id = "call_batch_6";
    tool_calls[2].name = "read_file";
    tool_calls[2].arguments = "{\"path\": \"/tmp/b.txt\"}";

    ApprovalBatchResult batch = {0};

    /* Overall should be denied because one gated tool can't be prompted */
    ApprovalResult result = check_approval_gate_batch(&config, tool_calls, 3, &batch);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, result);
    TEST_ASSERT_EQUAL(3, batch.count);

    /* Check individual results */
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[0]);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, batch.results[1]);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[2]);

    free_batch_result(&batch);
}

void test_batch_with_allowlist_bypass(void) {
    /* Add allowlist for shell git commands */
    const char *prefix[] = {"git"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Create multiple git commands */
    ToolCall tool_calls[2];
    memset(tool_calls, 0, sizeof(tool_calls));

    tool_calls[0].id = "call_batch_7";
    tool_calls[0].name = "shell";
    tool_calls[0].arguments = "{\"command\": \"git status\"}";

    tool_calls[1].id = "call_batch_8";
    tool_calls[1].name = "shell";
    tool_calls[1].arguments = "{\"command\": \"git log --oneline -5\"}";

    ApprovalBatchResult batch = {0};

    /* Both should be allowed via allowlist */
    ApprovalResult result = check_approval_gate_batch(&config, tool_calls, 2, &batch);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);
    TEST_ASSERT_EQUAL(2, batch.count);

    for (int i = 0; i < batch.count; i++) {
        TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[i]);
    }

    free_batch_result(&batch);
}

/* =============================================================================
 * Category Configuration Tests
 * ========================================================================== */

void test_denied_category_blocks_all(void) {
    /* Set shell category to DENY */
    config.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_DENY;

    /* Even if on allowlist, DENY should block */
    const char *prefix[] = {"echo"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    ToolCall tool_call = {0};
    tool_call.id = "call_011";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"echo hello\"}";

    ApprovedPath path = {0};

    /* Should be denied due to category DENY */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_DENIED, result);

    free_approved_path(&path);
}

void test_yolo_mode_allows_all(void) {
    /* Enable yolo mode */
    approval_gate_enable_yolo(&config);

    /* Create a normally gated shell command */
    ToolCall tool_call = {0};
    tool_call.id = "call_012";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"rm -rf /tmp/test\"}";

    ApprovedPath path = {0};

    /* Should be allowed in yolo mode */
    ApprovalResult result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);

    free_approved_path(&path);
}

/* =============================================================================
 * CLI Allow Entry Tests
 * ========================================================================== */

void test_cli_allow_shell_entry(void) {
    /* Add CLI allow entry for shell */
    int result = approval_gate_add_cli_allow(&config, "shell:make,test");
    TEST_ASSERT_EQUAL(0, result);

    /* Create matching shell command */
    ToolCall tool_call = {0};
    tool_call.id = "call_013";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"make test\"}";

    ApprovedPath path = {0};

    /* Should be allowed via CLI entry */
    ApprovalResult gate_result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, gate_result);

    free_approved_path(&path);
}

void test_cli_allow_regex_entry(void) {
    /* Add CLI allow entry for write_file (regex)
     * Note: The regex matches against full arguments JSON */
    int result = approval_gate_add_cli_allow(&config, "write_file:/home/user/");
    TEST_ASSERT_EQUAL(0, result);

    /* Create matching tool call */
    ToolCall tool_call = {0};
    tool_call.id = "call_014";
    tool_call.name = "write_file";
    tool_call.arguments = "{\"path\": \"/home/user/test.txt\"}";

    ApprovedPath path = {0};

    /* Should be allowed via CLI entry */
    ApprovalResult gate_result = check_approval_gate(&config, &tool_call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, gate_result);

    free_approved_path(&path);
}

/* =============================================================================
 * Error Formatting Tests
 * ========================================================================== */

void test_format_denial_error(void) {
    ToolCall tool_call = {0};
    tool_call.id = "call_015";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"dangerous\"}";

    char *error = format_denial_error(&tool_call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_NOT_NULL(strstr(error, "operation_denied"));
    TEST_ASSERT_NOT_NULL(strstr(error, "shell"));

    free(error);
}

void test_format_rate_limit_error(void) {
    ToolCall tool_call = {0};
    tool_call.id = "call_016";
    tool_call.name = "test_tool";
    tool_call.arguments = "{}";

    /* Track denials to trigger rate limiting */
    for (int i = 0; i < 5; i++) {
        track_denial(&config, &tool_call);
    }

    char *error = format_rate_limit_error(&config, &tool_call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_NOT_NULL(strstr(error, "rate_limited"));
    TEST_ASSERT_NOT_NULL(strstr(error, "retry_after"));

    free(error);
}

void test_format_non_interactive_error(void) {
    ToolCall tool_call = {0};
    tool_call.id = "call_017";
    tool_call.name = "shell";
    tool_call.arguments = "{\"command\": \"test\"}";

    char *error = format_non_interactive_error(&tool_call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_NOT_NULL(strstr(error, "non_interactive"));
    TEST_ASSERT_NOT_NULL(strstr(error, "shell"));

    free(error);
}

/* =============================================================================
 * Pattern Generator Integration Tests
 * ========================================================================== */

void test_pattern_generator_for_file_path(void) {
    GeneratedPattern pattern = {0};

    int result = generate_file_path_pattern("/tmp/test/file.txt", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    /* For /tmp paths, should get exact match or path-based pattern */
    TEST_ASSERT_NULL(pattern.command_prefix);  /* Not a shell command */

    free_generated_pattern(&pattern);
}

void test_pattern_generator_for_shell_command(void) {
    GeneratedPattern pattern = {0};

    int result = generate_shell_command_pattern("git status", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.command_prefix);
    TEST_ASSERT_GREATER_THAN(0, pattern.prefix_len);

    /* First prefix should be "git" */
    TEST_ASSERT_EQUAL_STRING("git", pattern.command_prefix[0]);

    free_generated_pattern(&pattern);
}

/* =============================================================================
 * Subagent Inheritance Tests
 * ========================================================================== */

void test_child_inherits_category_config(void) {
    /* Modify parent config */
    config.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_ALLOW;
    config.categories[GATE_CATEGORY_NETWORK] = GATE_ACTION_DENY;

    /* Create child from parent */
    ApprovalGateConfig child = {0};
    int result = approval_gate_init_from_parent(&child, &config);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should inherit category settings */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, child.categories[GATE_CATEGORY_SHELL]);
    TEST_ASSERT_EQUAL(GATE_ACTION_DENY, child.categories[GATE_CATEGORY_NETWORK]);

    approval_gate_cleanup(&child);
}

void test_child_inherits_static_allowlist_not_session(void) {
    /* Add a "static" allowlist entry and mark it.
     * In production, static entries come from config file load and static_allowlist_count
     * is set by the config parser. Here we simulate that by manually setting the count
     * to test the inheritance boundary between static and session entries. */
    approval_gate_add_allowlist(&config, "write_file", "^/static/.*$");
    config.static_allowlist_count = config.allowlist_count;

    /* Add a session allowlist entry (added after static count set) */
    approval_gate_add_allowlist(&config, "write_file", "^/session/.*$");

    /* Create child */
    ApprovalGateConfig child = {0};
    int result = approval_gate_init_from_parent(&child, &config);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should have only static entry */
    TEST_ASSERT_EQUAL(1, child.allowlist_count);
    TEST_ASSERT_EQUAL_STRING("^/static/.*$", child.allowlist[0].pattern);

    approval_gate_cleanup(&child);
}

/* =============================================================================
 * Batch Result Memory Tests
 * ========================================================================== */

void test_batch_result_init_and_free(void) {
    ApprovalBatchResult batch = {0};

    int result = init_batch_result(&batch, 5);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(5, batch.count);
    TEST_ASSERT_NOT_NULL(batch.results);
    TEST_ASSERT_NOT_NULL(batch.paths);

    /* Initialize results */
    for (int i = 0; i < 5; i++) {
        batch.results[i] = APPROVAL_ALLOWED;
    }

    /* Free should not crash */
    free_batch_result(&batch);

    /* After free, pointers should be NULL and count should be 0 */
    TEST_ASSERT_NULL(batch.results);
    TEST_ASSERT_NULL(batch.paths);
    TEST_ASSERT_EQUAL(0, batch.count);
}

void test_batch_result_free_handles_null(void) {
    /* Should not crash on NULL or uninitialized batch */
    ApprovalBatchResult batch = {0};
    free_batch_result(&batch);
    free_batch_result(NULL);
}

/* =============================================================================
 * Main Test Runner
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Non-Interactive Mode Tests */
    RUN_TEST(test_non_interactive_denies_gated_operations);
    RUN_TEST(test_non_interactive_allows_allowed_category);
    RUN_TEST(test_non_interactive_with_category_override);

    /* Allowlist Bypass Tests */
    RUN_TEST(test_allowlist_regex_bypasses_gate);
    RUN_TEST(test_allowlist_non_matching_requires_gate);
    RUN_TEST(test_shell_allowlist_prefix_matching);
    RUN_TEST(test_shell_allowlist_chain_blocked);

    /* Rate Limiting Tests */
    RUN_TEST(test_rate_limiting_after_denials);
    RUN_TEST(test_rate_limiting_reset_on_approval);
    RUN_TEST(test_rate_limiting_different_tools_independent);

    /* Batch Approval Tests */
    RUN_TEST(test_batch_all_allowed_category);
    RUN_TEST(test_batch_mixed_categories_non_interactive);
    RUN_TEST(test_batch_with_allowlist_bypass);

    /* Category Configuration Tests */
    RUN_TEST(test_denied_category_blocks_all);
    RUN_TEST(test_yolo_mode_allows_all);

    /* CLI Allow Entry Tests */
    RUN_TEST(test_cli_allow_shell_entry);
    RUN_TEST(test_cli_allow_regex_entry);

    /* Error Formatting Tests */
    RUN_TEST(test_format_denial_error);
    RUN_TEST(test_format_rate_limit_error);
    RUN_TEST(test_format_non_interactive_error);

    /* Pattern Generator Tests */
    RUN_TEST(test_pattern_generator_for_file_path);
    RUN_TEST(test_pattern_generator_for_shell_command);

    /* Subagent Inheritance Tests */
    RUN_TEST(test_child_inherits_category_config);
    RUN_TEST(test_child_inherits_static_allowlist_not_session);

    /* Batch Result Memory Tests */
    RUN_TEST(test_batch_result_init_and_free);
    RUN_TEST(test_batch_result_free_handles_null);

    return UNITY_END();
}
