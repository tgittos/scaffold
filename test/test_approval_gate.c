/**
 * Unit tests for approval gate module
 */

#include "../test/unity/unity.h"
#include "../src/core/approval_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Test fixture */
static ApprovalGateConfig config;

void setUp(void) {
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);
}

void tearDown(void) {
    approval_gate_cleanup(&config);
}

/* =============================================================================
 * Initialization Tests
 * ========================================================================== */

void test_approval_gate_init_creates_valid_config(void) {
    ApprovalGateConfig test_config;
    int result = approval_gate_init(&test_config);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, test_config.enabled);
    TEST_ASSERT_NOT_NULL(test_config.allowlist);
    TEST_ASSERT_NOT_NULL(test_config.shell_allowlist);
    TEST_ASSERT_NOT_NULL(test_config.denial_trackers);
    TEST_ASSERT_NULL(test_config.approval_channel);

    approval_gate_cleanup(&test_config);
}

void test_approval_gate_init_sets_default_categories(void) {
    /* Default categories should be set as per spec */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_FILE_WRITE]);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_FILE_READ]);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_SHELL]);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_NETWORK]);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_MEMORY]);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_SUBAGENT]);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_MCP]);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_PYTHON]);
}

void test_approval_gate_init_null_returns_error(void) {
    int result = approval_gate_init(NULL);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_approval_gate_cleanup_handles_null(void) {
    /* Should not crash */
    approval_gate_cleanup(NULL);
}

void test_approval_gate_init_from_parent(void) {
    ApprovalGateConfig parent;
    ApprovalGateConfig child;

    int result = approval_gate_init(&parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Modify parent config */
    parent.categories[GATE_CATEGORY_FILE_WRITE] = GATE_ACTION_ALLOW;
    parent.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_DENY;
    parent.enabled = 0;

    result = approval_gate_init_from_parent(&child, &parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should inherit parent's configuration */
    TEST_ASSERT_EQUAL(0, child.enabled);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, child.categories[GATE_CATEGORY_FILE_WRITE]);
    TEST_ASSERT_EQUAL(GATE_ACTION_DENY, child.categories[GATE_CATEGORY_SHELL]);

    approval_gate_cleanup(&parent);
    approval_gate_cleanup(&child);
}

/* =============================================================================
 * Category Mapping Tests
 * ========================================================================== */

void test_get_tool_category_memory_tools(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("remember"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("recall_memories"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("forget_memory"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("todo"));
}

void test_get_tool_category_vector_db_prefix(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("vector_db_add"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("vector_db_search"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, get_tool_category("vector_db_delete"));
}

void test_get_tool_category_mcp_prefix(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MCP, get_tool_category("mcp_anything"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MCP, get_tool_category("mcp_tool"));
}

void test_get_tool_category_file_tools(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("read_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("list_dir"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("search_files"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("file_info"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("process_pdf_document"));

    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("write_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("append_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("apply_delta"));
}

void test_get_tool_category_shell(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SHELL, get_tool_category("shell"));
}

void test_get_tool_category_network(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_NETWORK, get_tool_category("web_fetch"));
}

void test_get_tool_category_subagent(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SUBAGENT, get_tool_category("subagent"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SUBAGENT, get_tool_category("subagent_status"));
}

void test_get_tool_category_python(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category("python"));
}

void test_get_tool_category_unknown_defaults_to_python(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category("unknown_tool"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category("custom_dynamic_tool"));
}

void test_get_tool_category_null_defaults_to_python(void) {
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, get_tool_category(NULL));
}

/* =============================================================================
 * Rate Limiting Tests
 * ========================================================================== */

void test_rate_limiting_initial_state(void) {
    ToolCall tool_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* Initially not rate limited */
    TEST_ASSERT_EQUAL(0, is_rate_limited(&config, &tool_call));
    TEST_ASSERT_EQUAL(0, get_rate_limit_remaining(&config, "shell"));
}

void test_rate_limiting_one_denial_no_backoff(void) {
    ToolCall tool_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* 1 denial = no backoff */
    track_denial(&config, &tool_call);
    TEST_ASSERT_EQUAL(0, is_rate_limited(&config, &tool_call));
}

void test_rate_limiting_two_denials_no_backoff(void) {
    ToolCall tool_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* 2 denials = no backoff */
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);
    TEST_ASSERT_EQUAL(0, is_rate_limited(&config, &tool_call));
}

void test_rate_limiting_three_denials_backoff(void) {
    ToolCall tool_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* 3 denials = 5 second backoff */
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);

    TEST_ASSERT_EQUAL(1, is_rate_limited(&config, &tool_call));
    int remaining = get_rate_limit_remaining(&config, "shell");
    TEST_ASSERT_TRUE(remaining > 0 && remaining <= 5);
}

void test_rate_limiting_reset(void) {
    ToolCall tool_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* Build up denials */
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);
    track_denial(&config, &tool_call);

    TEST_ASSERT_EQUAL(1, is_rate_limited(&config, &tool_call));

    /* Reset tracker */
    reset_denial_tracker(&config, "shell");

    TEST_ASSERT_EQUAL(0, is_rate_limited(&config, &tool_call));
    TEST_ASSERT_EQUAL(0, get_rate_limit_remaining(&config, "shell"));
}

void test_rate_limiting_per_tool(void) {
    ToolCall shell_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{}"
    };

    ToolCall write_call = {
        .id = "call_2",
        .name = "write_file",
        .arguments = "{}"
    };

    /* Only shell gets denied */
    track_denial(&config, &shell_call);
    track_denial(&config, &shell_call);
    track_denial(&config, &shell_call);

    /* Shell is rate limited, write_file is not */
    TEST_ASSERT_EQUAL(1, is_rate_limited(&config, &shell_call));
    TEST_ASSERT_EQUAL(0, is_rate_limited(&config, &write_call));
}

/* =============================================================================
 * Allowlist Tests
 * ========================================================================== */

void test_add_allowlist_entry(void) {
    int result = approval_gate_add_allowlist(&config, "write_file", "^\\./src/.*\\.c$");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, config.allowlist_count);
    TEST_ASSERT_EQUAL_STRING("write_file", config.allowlist[0].tool);
    TEST_ASSERT_EQUAL_STRING("^\\./src/.*\\.c$", config.allowlist[0].pattern);
    TEST_ASSERT_EQUAL(1, config.allowlist[0].valid);
}

void test_add_shell_allowlist_entry(void) {
    const char *prefix[] = {"git", "status"};
    int result = approval_gate_add_shell_allowlist(&config, prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, config.shell_allowlist_count);
    TEST_ASSERT_EQUAL(2, config.shell_allowlist[0].prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", config.shell_allowlist[0].command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("status", config.shell_allowlist[0].command_prefix[1]);
}

void test_allowlist_invalid_regex(void) {
    /* Invalid regex should still add entry but mark as invalid */
    int result = approval_gate_add_allowlist(&config, "test", "[invalid(regex");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, config.allowlist_count);
    TEST_ASSERT_EQUAL(0, config.allowlist[0].valid);
}

void test_allowlist_matches_pattern(void) {
    approval_gate_add_allowlist(&config, "write_file", "^test_.*\\.c$");

    ToolCall match_call = {
        .id = "call_1",
        .name = "write_file",
        .arguments = "test_foo.c"
    };

    ToolCall no_match_call = {
        .id = "call_2",
        .name = "write_file",
        .arguments = "production.c"
    };

    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &match_call));
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &no_match_call));
}

void test_allowlist_requires_tool_match(void) {
    approval_gate_add_allowlist(&config, "write_file", ".*");

    ToolCall wrong_tool = {
        .id = "call_1",
        .name = "append_file",  /* Different tool */
        .arguments = "anything"
    };

    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &wrong_tool));
}

/* =============================================================================
 * Approval Checking Tests
 * ========================================================================== */

void test_approval_requires_check_allowed_category(void) {
    ToolCall call = {
        .id = "call_1",
        .name = "read_file",  /* file_read category = allow */
        .arguments = "{}"
    };

    int result = approval_gate_requires_check(&config, &call);
    TEST_ASSERT_EQUAL(0, result);  /* 0 = allowed */
}

void test_approval_requires_check_gated_category(void) {
    ToolCall call = {
        .id = "call_1",
        .name = "shell",  /* shell category = gate */
        .arguments = "{}"
    };

    int result = approval_gate_requires_check(&config, &call);
    TEST_ASSERT_EQUAL(1, result);  /* 1 = requires approval */
}

void test_approval_requires_check_denied_category(void) {
    /* Set shell to deny */
    config.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_DENY;

    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{}"
    };

    int result = approval_gate_requires_check(&config, &call);
    TEST_ASSERT_EQUAL(-1, result);  /* -1 = denied */
}

void test_approval_requires_check_gates_disabled(void) {
    config.enabled = 0;

    ToolCall call = {
        .id = "call_1",
        .name = "shell",  /* Would normally be gated */
        .arguments = "{}"
    };

    int result = approval_gate_requires_check(&config, &call);
    TEST_ASSERT_EQUAL(0, result);  /* 0 = allowed when disabled */
}

void test_approval_requires_check_allowlist_bypass(void) {
    approval_gate_add_allowlist(&config, "write_file", ".*\\.test\\.c$");

    ToolCall match_call = {
        .id = "call_1",
        .name = "write_file",
        .arguments = "foo.test.c"
    };

    ToolCall no_match_call = {
        .id = "call_2",
        .name = "write_file",
        .arguments = "production.c"
    };

    /* Matched by allowlist = allowed */
    TEST_ASSERT_EQUAL(0, approval_gate_requires_check(&config, &match_call));

    /* Not matched = requires approval */
    TEST_ASSERT_EQUAL(1, approval_gate_requires_check(&config, &no_match_call));
}

/* =============================================================================
 * Shell Detection Tests
 * ========================================================================== */

void test_detect_shell_type(void) {
    ShellType type = detect_shell_type();

    /* Should return a valid shell type */
    TEST_ASSERT_TRUE(type == SHELL_TYPE_POSIX ||
                     type == SHELL_TYPE_CMD ||
                     type == SHELL_TYPE_POWERSHELL ||
                     type == SHELL_TYPE_UNKNOWN);

    /* On Linux, should typically be POSIX */
#ifndef _WIN32
    TEST_ASSERT_EQUAL(SHELL_TYPE_POSIX, type);
#endif
}

/* =============================================================================
 * Utility Function Tests
 * ========================================================================== */

void test_gate_category_name(void) {
    TEST_ASSERT_EQUAL_STRING("file_write", gate_category_name(GATE_CATEGORY_FILE_WRITE));
    TEST_ASSERT_EQUAL_STRING("file_read", gate_category_name(GATE_CATEGORY_FILE_READ));
    TEST_ASSERT_EQUAL_STRING("shell", gate_category_name(GATE_CATEGORY_SHELL));
    TEST_ASSERT_EQUAL_STRING("network", gate_category_name(GATE_CATEGORY_NETWORK));
    TEST_ASSERT_EQUAL_STRING("memory", gate_category_name(GATE_CATEGORY_MEMORY));
    TEST_ASSERT_EQUAL_STRING("subagent", gate_category_name(GATE_CATEGORY_SUBAGENT));
    TEST_ASSERT_EQUAL_STRING("mcp", gate_category_name(GATE_CATEGORY_MCP));
    TEST_ASSERT_EQUAL_STRING("python", gate_category_name(GATE_CATEGORY_PYTHON));
    TEST_ASSERT_EQUAL_STRING("unknown", gate_category_name(-1));
    TEST_ASSERT_EQUAL_STRING("unknown", gate_category_name(GATE_CATEGORY_COUNT));
}

void test_gate_action_name(void) {
    TEST_ASSERT_EQUAL_STRING("allow", gate_action_name(GATE_ACTION_ALLOW));
    TEST_ASSERT_EQUAL_STRING("gate", gate_action_name(GATE_ACTION_GATE));
    TEST_ASSERT_EQUAL_STRING("deny", gate_action_name(GATE_ACTION_DENY));
    TEST_ASSERT_EQUAL_STRING("unknown", gate_action_name(-1));
    TEST_ASSERT_EQUAL_STRING("unknown", gate_action_name(100));
}

void test_approval_result_name(void) {
    TEST_ASSERT_EQUAL_STRING("allowed", approval_result_name(APPROVAL_ALLOWED));
    TEST_ASSERT_EQUAL_STRING("denied", approval_result_name(APPROVAL_DENIED));
    TEST_ASSERT_EQUAL_STRING("allowed_always", approval_result_name(APPROVAL_ALLOWED_ALWAYS));
    TEST_ASSERT_EQUAL_STRING("aborted", approval_result_name(APPROVAL_ABORTED));
    TEST_ASSERT_EQUAL_STRING("rate_limited", approval_result_name(APPROVAL_RATE_LIMITED));
    TEST_ASSERT_EQUAL_STRING("unknown", approval_result_name(-1));
}

void test_verify_result_message(void) {
    TEST_ASSERT_EQUAL_STRING("Path verified successfully", verify_result_message(VERIFY_OK));
    TEST_ASSERT_EQUAL_STRING("Path is a symbolic link", verify_result_message(VERIFY_ERR_SYMLINK));
    TEST_ASSERT_EQUAL_STRING("File was deleted after approval", verify_result_message(VERIFY_ERR_DELETED));
    TEST_ASSERT_EQUAL_STRING("Failed to open file", verify_result_message(VERIFY_ERR_OPEN));
    TEST_ASSERT_EQUAL_STRING("File changed since approval", verify_result_message(VERIFY_ERR_INODE_MISMATCH));
}

/* =============================================================================
 * Error Formatting Tests
 * ========================================================================== */

void test_format_denial_error(void) {
    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{}"
    };

    char *error = format_denial_error(&call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_TRUE(strstr(error, "operation_denied") != NULL);
    TEST_ASSERT_TRUE(strstr(error, "shell") != NULL);
    free(error);
}

void test_format_protected_file_error(void) {
    char *error = format_protected_file_error("/path/to/.env");
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_TRUE(strstr(error, "protected_file") != NULL);
    TEST_ASSERT_TRUE(strstr(error, ".env") != NULL);
    free(error);
}

void test_format_rate_limit_error(void) {
    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{}"
    };

    /* Build up rate limit */
    track_denial(&config, &call);
    track_denial(&config, &call);
    track_denial(&config, &call);

    char *error = format_rate_limit_error(&config, &call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_TRUE(strstr(error, "rate_limited") != NULL);
    TEST_ASSERT_TRUE(strstr(error, "shell") != NULL);
    TEST_ASSERT_TRUE(strstr(error, "retry_after") != NULL);
    free(error);
}

void test_format_verify_error(void) {
    char *error = format_verify_error(VERIFY_ERR_SYMLINK, "/path/to/file");
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_TRUE(strstr(error, "path_changed") != NULL);
    TEST_ASSERT_TRUE(strstr(error, "symbolic link") != NULL);
    free(error);
}

/* =============================================================================
 * Path Verification Tests
 * ========================================================================== */

void test_free_approved_path_handles_null(void) {
    /* Should not crash */
    free_approved_path(NULL);

    ApprovedPath empty = {0};
    free_approved_path(&empty);
}

void test_verify_approved_path_null_path(void) {
    ApprovedPath path = {0};
    path.resolved_path = NULL;

    VerifyResult result = verify_approved_path(&path);
    TEST_ASSERT_EQUAL(VERIFY_ERR_OPEN, result);
}

/* =============================================================================
 * Main
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Initialization tests */
    RUN_TEST(test_approval_gate_init_creates_valid_config);
    RUN_TEST(test_approval_gate_init_sets_default_categories);
    RUN_TEST(test_approval_gate_init_null_returns_error);
    RUN_TEST(test_approval_gate_cleanup_handles_null);
    RUN_TEST(test_approval_gate_init_from_parent);

    /* Category mapping tests */
    RUN_TEST(test_get_tool_category_memory_tools);
    RUN_TEST(test_get_tool_category_vector_db_prefix);
    RUN_TEST(test_get_tool_category_mcp_prefix);
    RUN_TEST(test_get_tool_category_file_tools);
    RUN_TEST(test_get_tool_category_shell);
    RUN_TEST(test_get_tool_category_network);
    RUN_TEST(test_get_tool_category_subagent);
    RUN_TEST(test_get_tool_category_python);
    RUN_TEST(test_get_tool_category_unknown_defaults_to_python);
    RUN_TEST(test_get_tool_category_null_defaults_to_python);

    /* Rate limiting tests */
    RUN_TEST(test_rate_limiting_initial_state);
    RUN_TEST(test_rate_limiting_one_denial_no_backoff);
    RUN_TEST(test_rate_limiting_two_denials_no_backoff);
    RUN_TEST(test_rate_limiting_three_denials_backoff);
    RUN_TEST(test_rate_limiting_reset);
    RUN_TEST(test_rate_limiting_per_tool);

    /* Allowlist tests */
    RUN_TEST(test_add_allowlist_entry);
    RUN_TEST(test_add_shell_allowlist_entry);
    RUN_TEST(test_allowlist_invalid_regex);
    RUN_TEST(test_allowlist_matches_pattern);
    RUN_TEST(test_allowlist_requires_tool_match);

    /* Approval checking tests */
    RUN_TEST(test_approval_requires_check_allowed_category);
    RUN_TEST(test_approval_requires_check_gated_category);
    RUN_TEST(test_approval_requires_check_denied_category);
    RUN_TEST(test_approval_requires_check_gates_disabled);
    RUN_TEST(test_approval_requires_check_allowlist_bypass);

    /* Shell detection tests */
    RUN_TEST(test_detect_shell_type);

    /* Utility function tests */
    RUN_TEST(test_gate_category_name);
    RUN_TEST(test_gate_action_name);
    RUN_TEST(test_approval_result_name);
    RUN_TEST(test_verify_result_message);

    /* Error formatting tests */
    RUN_TEST(test_format_denial_error);
    RUN_TEST(test_format_protected_file_error);
    RUN_TEST(test_format_rate_limit_error);
    RUN_TEST(test_format_verify_error);

    /* Path verification tests */
    RUN_TEST(test_free_approved_path_handles_null);
    RUN_TEST(test_verify_approved_path_null_path);

    return UNITY_END();
}
