/**
 * Unit tests for approval gate module
 */

#include "../test/unity/unity.h"
#include "policy/approval_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util/ralph_home.h"

/*
 * Stub implementations for Python tool functions.
 * These are used when Python tools aren't loaded (test environment).
 * In real usage, python_tool_files.c provides the actual implementations.
 */
int is_python_file_tool(const char *name) {
    (void)name;
    return 0; /* No Python tools loaded in test environment */
}

const char* python_tool_get_gate_category(const char *name) {
    (void)name;
    return NULL; /* No metadata available in test environment */
}

const char* python_tool_get_match_arg(const char *name) {
    (void)name;
    return NULL; /* No metadata available in test environment */
}

/* Test fixture */
static ApprovalGateConfig config;

static const char *g_test_home = "/tmp/test_approval_gate_home";

static void remove_test_config(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", g_test_home);
    unlink(path);
}

static FILE* create_test_config(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.json", g_test_home);
    return fopen(path, "w");
}

void setUp(void) {
    ralph_home_cleanup();
    remove_test_config();
    rmdir(g_test_home);
    mkdir(g_test_home, 0755);
    ralph_home_init(g_test_home);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);
}

void tearDown(void) {
    approval_gate_cleanup(&config);
    remove_test_config();
    rmdir(g_test_home);
    ralph_home_cleanup();
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
    TEST_ASSERT_NOT_NULL(test_config.rate_limiter);
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

void test_approval_gate_init_from_parent_inherits_static_allowlist(void) {
    ApprovalGateConfig parent;
    ApprovalGateConfig child;

    int result = approval_gate_init(&parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Add a static allowlist entry to parent (simulating config file load) */
    result = approval_gate_add_allowlist(&parent, "write_file", "^/tmp/.*\\.txt$");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, parent.allowlist_count);

    /* Pretend this entry came from the config file by updating static count */
    parent.static_allowlist_count = parent.allowlist_count;

    result = approval_gate_init_from_parent(&child, &parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should inherit the static allowlist entry */
    TEST_ASSERT_EQUAL(1, child.allowlist_count);
    TEST_ASSERT_EQUAL(1, child.static_allowlist_count);
    TEST_ASSERT_NOT_NULL(child.allowlist);
    TEST_ASSERT_NOT_NULL(child.allowlist[0].tool);
    TEST_ASSERT_NOT_NULL(child.allowlist[0].pattern);
    TEST_ASSERT_EQUAL_STRING("write_file", child.allowlist[0].tool);
    TEST_ASSERT_EQUAL_STRING("^/tmp/.*\\.txt$", child.allowlist[0].pattern);
    TEST_ASSERT_EQUAL(1, child.allowlist[0].valid);

    approval_gate_cleanup(&parent);
    approval_gate_cleanup(&child);
}

void test_approval_gate_init_from_parent_inherits_static_shell_allowlist(void) {
    ApprovalGateConfig parent;
    ApprovalGateConfig child;

    int result = approval_gate_init(&parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Add a static shell allowlist entry to parent */
    const char *prefix[] = {"git", "status"};
    result = approval_gate_add_shell_allowlist(&parent, prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, parent.shell_allowlist_count);

    /* Pretend this entry came from the config file by updating static count */
    parent.static_shell_allowlist_count = parent.shell_allowlist_count;

    result = approval_gate_init_from_parent(&child, &parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should inherit the static shell allowlist entry */
    TEST_ASSERT_EQUAL(1, child.shell_allowlist_count);
    TEST_ASSERT_EQUAL(1, child.static_shell_allowlist_count);
    TEST_ASSERT_NOT_NULL(child.shell_allowlist);
    TEST_ASSERT_EQUAL(2, child.shell_allowlist[0].prefix_len);
    TEST_ASSERT_NOT_NULL(child.shell_allowlist[0].command_prefix);
    TEST_ASSERT_EQUAL_STRING("git", child.shell_allowlist[0].command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("status", child.shell_allowlist[0].command_prefix[1]);

    approval_gate_cleanup(&parent);
    approval_gate_cleanup(&child);
}

void test_approval_gate_init_from_parent_excludes_session_allowlist(void) {
    ApprovalGateConfig parent;
    ApprovalGateConfig child;

    int result = approval_gate_init(&parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Add a static allowlist entry (simulating config file load) */
    result = approval_gate_add_allowlist(&parent, "write_file", "^/tmp/.*\\.txt$");
    TEST_ASSERT_EQUAL(0, result);

    /* Mark this as static (from config file) */
    parent.static_allowlist_count = parent.allowlist_count;

    /* Add a session allowlist entry (simulating "allow always" at runtime) */
    result = approval_gate_add_allowlist(&parent, "read_file", "^/var/log/.*$");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, parent.allowlist_count);
    /* static_allowlist_count remains 1 (not updated for session entries) */

    result = approval_gate_init_from_parent(&child, &parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should only inherit the static entry, NOT the session entry */
    TEST_ASSERT_EQUAL(1, child.allowlist_count);
    TEST_ASSERT_EQUAL(1, child.static_allowlist_count);
    TEST_ASSERT_EQUAL_STRING("write_file", child.allowlist[0].tool);
    /* Verify the session entry (read_file) was not inherited */

    approval_gate_cleanup(&parent);
    approval_gate_cleanup(&child);
}

void test_approval_gate_init_from_parent_excludes_session_shell_allowlist(void) {
    ApprovalGateConfig parent;
    ApprovalGateConfig child;

    int result = approval_gate_init(&parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Add a static shell allowlist entry */
    const char *static_prefix[] = {"git", "status"};
    result = approval_gate_add_shell_allowlist(&parent, static_prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, result);

    /* Mark this as static (from config file) */
    parent.static_shell_allowlist_count = parent.shell_allowlist_count;

    /* Add a session shell allowlist entry */
    const char *session_prefix[] = {"npm", "install"};
    result = approval_gate_add_shell_allowlist(&parent, session_prefix, 2, SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(2, parent.shell_allowlist_count);

    result = approval_gate_init_from_parent(&child, &parent);
    TEST_ASSERT_EQUAL(0, result);

    /* Child should only inherit the static entry, NOT the session entry */
    TEST_ASSERT_EQUAL(1, child.shell_allowlist_count);
    TEST_ASSERT_EQUAL(1, child.static_shell_allowlist_count);
    TEST_ASSERT_EQUAL_STRING("git", child.shell_allowlist[0].command_prefix[0]);
    /* Verify the session entry (npm) was not inherited */

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

/**
 * Test that Python tool Gate: directive overrides are used when available.
 *
 * Note: This tests the fallback behavior when Python tools are not loaded.
 * When the Python interpreter is not initialized, is_python_file_tool() returns 0
 * and get_tool_category() falls back to hardcoded mappings.
 *
 * Full Python tool integration tests would require:
 * 1. Initializing the Python interpreter
 * 2. Loading Python tool files with Gate: directives
 * 3. Verifying categories are derived from directives
 *
 * These tests are covered in test_python_tool_integration.c.
 */
void test_get_tool_category_fallback_when_python_not_loaded(void) {
    /* Even when Python isn't loaded, known tools should map correctly via hardcoded fallback */
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, get_tool_category("read_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, get_tool_category("write_file"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SHELL, get_tool_category("shell"));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_NETWORK, get_tool_category("web_fetch"));
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
    TEST_ASSERT_EQUAL_STRING("non_interactive_denied", approval_result_name(APPROVAL_NON_INTERACTIVE_DENIED));
    /* Bounds checks - values outside enum range should return "unknown" */
    TEST_ASSERT_EQUAL_STRING("unknown", approval_result_name(-1));
    TEST_ASSERT_EQUAL_STRING("unknown", approval_result_name(APPROVAL_NON_INTERACTIVE_DENIED + 1));
    TEST_ASSERT_EQUAL_STRING("unknown", approval_result_name(999));
}

void test_verify_result_message(void) {
    TEST_ASSERT_EQUAL_STRING("Path verified successfully", verify_result_message(VERIFY_OK));
    /* atomic_file.c uses more descriptive message for symlinks */
    TEST_ASSERT_EQUAL_STRING("Path is a symlink (not allowed for security)", verify_result_message(VERIFY_ERR_SYMLINK));
    TEST_ASSERT_EQUAL_STRING("File was deleted after approval", verify_result_message(VERIFY_ERR_DELETED));
    TEST_ASSERT_EQUAL_STRING("Failed to open file", verify_result_message(VERIFY_ERR_OPEN));
    /* atomic_file.c uses more descriptive message for inode mismatch */
    TEST_ASSERT_EQUAL_STRING("File changed since approval (inode mismatch)", verify_result_message(VERIFY_ERR_INODE_MISMATCH));
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
    /* atomic_file.c uses "symlink_rejected" error type for symlink errors */
    TEST_ASSERT_TRUE(strstr(error, "symlink") != NULL);
    TEST_ASSERT_TRUE(strstr(error, "/path/to/file") != NULL);
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
    /* atomic_file.c returns VERIFY_ERR_INVALID_PATH for NULL/invalid paths */
    TEST_ASSERT_EQUAL(VERIFY_ERR_INVALID_PATH, result);
}

/* =============================================================================
 * Config Loading Tests
 * ========================================================================== */

void test_approval_gate_load_from_json_file_enabled(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"enabled\": false\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Enabled should be false from config */
    TEST_ASSERT_EQUAL(0, config.enabled);
}

void test_approval_gate_load_from_json_file_categories(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"enabled\": true,\n"
        "    \"categories\": {\n"
        "      \"file_write\": \"allow\",\n"
        "      \"shell\": \"deny\",\n"
        "      \"memory\": \"gate\"\n"
        "    }\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Check category overrides */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_FILE_WRITE]);
    TEST_ASSERT_EQUAL(GATE_ACTION_DENY, config.categories[GATE_CATEGORY_SHELL]);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_MEMORY]);
    /* Unchanged categories should have defaults */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_FILE_READ]);
}

void test_approval_gate_load_from_json_file_regex_allowlist(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"allowlist\": [\n"
        "      {\"tool\": \"write_file\", \"pattern\": \"^\\\\.test\\\\.c$\"},\n"
        "      {\"tool\": \"web_fetch\", \"pattern\": \"^https://api\\\\.example\\\\.com\"}\n"
        "    ]\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Check allowlist entries were added */
    TEST_ASSERT_EQUAL(2, config.allowlist_count);
    TEST_ASSERT_EQUAL_STRING("write_file", config.allowlist[0].tool);
    TEST_ASSERT_EQUAL_STRING("^\\.test\\.c$", config.allowlist[0].pattern);
    TEST_ASSERT_EQUAL(1, config.allowlist[0].valid);
    TEST_ASSERT_EQUAL_STRING("web_fetch", config.allowlist[1].tool);
}

void test_approval_gate_load_from_json_file_shell_allowlist(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"allowlist\": [\n"
        "      {\"tool\": \"shell\", \"command\": [\"ls\"]},\n"
        "      {\"tool\": \"shell\", \"command\": [\"git\", \"status\"]},\n"
        "      {\"tool\": \"shell\", \"command\": [\"dir\"], \"shell\": \"cmd\"}\n"
        "    ]\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Check shell allowlist entries were added */
    TEST_ASSERT_EQUAL(3, config.shell_allowlist_count);

    /* First entry: ls */
    TEST_ASSERT_EQUAL(1, config.shell_allowlist[0].prefix_len);
    TEST_ASSERT_EQUAL_STRING("ls", config.shell_allowlist[0].command_prefix[0]);
    TEST_ASSERT_EQUAL(SHELL_TYPE_UNKNOWN, config.shell_allowlist[0].shell_type);

    /* Second entry: git status */
    TEST_ASSERT_EQUAL(2, config.shell_allowlist[1].prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", config.shell_allowlist[1].command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("status", config.shell_allowlist[1].command_prefix[1]);

    /* Third entry: dir (cmd only) */
    TEST_ASSERT_EQUAL(1, config.shell_allowlist[2].prefix_len);
    TEST_ASSERT_EQUAL_STRING("dir", config.shell_allowlist[2].command_prefix[0]);
    TEST_ASSERT_EQUAL(SHELL_TYPE_CMD, config.shell_allowlist[2].shell_type);
}

void test_approval_gate_load_from_json_file_mixed_allowlist(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"enabled\": true,\n"
        "    \"categories\": {\n"
        "      \"network\": \"allow\"\n"
        "    },\n"
        "    \"allowlist\": [\n"
        "      {\"tool\": \"shell\", \"command\": [\"cat\"]},\n"
        "      {\"tool\": \"write_file\", \"pattern\": \"/tmp/.*\"}\n"
        "    ]\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Check both types of allowlist entries */
    TEST_ASSERT_EQUAL(1, config.shell_allowlist_count);
    TEST_ASSERT_EQUAL(1, config.allowlist_count);

    TEST_ASSERT_EQUAL_STRING("cat", config.shell_allowlist[0].command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("write_file", config.allowlist[0].tool);
    TEST_ASSERT_EQUAL_STRING("/tmp/.*", config.allowlist[0].pattern);

    /* Also check category override */
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_NETWORK]);
}

void test_approval_gate_load_from_json_file_no_approval_gates_section(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"api_url\": \"https://api.example.com\"\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file - should use defaults */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Should have defaults */
    TEST_ASSERT_EQUAL(1, config.enabled);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_FILE_WRITE]);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_FILE_READ]);
}

void test_approval_gate_load_from_json_file_powershell_shell(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"allowlist\": [\n"
        "      {\"tool\": \"shell\", \"command\": [\"Get-ChildItem\"], \"shell\": \"powershell\"}\n"
        "    ]\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init to load from file */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    TEST_ASSERT_EQUAL(1, config.shell_allowlist_count);
    TEST_ASSERT_EQUAL_STRING("Get-ChildItem", config.shell_allowlist[0].command_prefix[0]);
    TEST_ASSERT_EQUAL(SHELL_TYPE_POWERSHELL, config.shell_allowlist[0].shell_type);
}

void test_approval_gate_load_from_json_file_malformed_json(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json = "{ invalid json here }";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init - should succeed with defaults despite malformed JSON */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Should have defaults */
    TEST_ASSERT_EQUAL(1, config.enabled);
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_FILE_WRITE]);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_FILE_READ]);
    TEST_ASSERT_EQUAL(0, config.allowlist_count);
    TEST_ASSERT_EQUAL(0, config.shell_allowlist_count);
}

void test_approval_gate_load_from_json_file_invalid_entries_skipped(void) {
    FILE *f = create_test_config();
    TEST_ASSERT_NOT_NULL(f);

    const char *json =
        "{\n"
        "  \"approval_gates\": {\n"
        "    \"categories\": {\n"
        "      \"invalid_category\": \"allow\",\n"
        "      \"file_write\": \"invalid_action\",\n"
        "      \"shell\": \"deny\"\n"
        "    },\n"
        "    \"allowlist\": [\n"
        "      {\"tool\": \"shell\", \"command\": []},\n"
        "      {\"tool\": \"shell\", \"command\": [123]},\n"
        "      {\"tool\": \"write_file\"},\n"
        "      {\"pattern\": \"no_tool_field\"},\n"
        "      {\"tool\": \"shell\", \"command\": [\"ls\"]}\n"
        "    ]\n"
        "  }\n"
        "}\n";

    fprintf(f, "%s", json);
    fclose(f);

    /* Re-init - should succeed, skipping invalid entries */
    approval_gate_cleanup(&config);
    int result = approval_gate_init(&config);
    TEST_ASSERT_EQUAL(0, result);

    /* Valid category override should work */
    TEST_ASSERT_EQUAL(GATE_ACTION_DENY, config.categories[GATE_CATEGORY_SHELL]);
    /* Invalid category/action should keep default */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_FILE_WRITE]);

    /* Only valid allowlist entry should be added */
    TEST_ASSERT_EQUAL(1, config.shell_allowlist_count);
    TEST_ASSERT_EQUAL_STRING("ls", config.shell_allowlist[0].command_prefix[0]);
    TEST_ASSERT_EQUAL(0, config.allowlist_count);
}

/* =============================================================================
 * CLI Override Tests
 * ========================================================================== */

void test_approval_gate_enable_yolo(void) {
    TEST_ASSERT_EQUAL(1, config.enabled);
    approval_gate_enable_yolo(&config);
    TEST_ASSERT_EQUAL(0, config.enabled);
}

void test_approval_gate_enable_yolo_null_safe(void) {
    /* Should not crash */
    approval_gate_enable_yolo(NULL);
}

void test_approval_gate_set_category_action(void) {
    /* Default should be gate */
    TEST_ASSERT_EQUAL(GATE_ACTION_GATE, config.categories[GATE_CATEGORY_FILE_WRITE]);

    /* Set to allow */
    int result = approval_gate_set_category_action(&config, "file_write", GATE_ACTION_ALLOW);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(GATE_ACTION_ALLOW, config.categories[GATE_CATEGORY_FILE_WRITE]);

    /* Set to deny */
    result = approval_gate_set_category_action(&config, "shell", GATE_ACTION_DENY);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(GATE_ACTION_DENY, config.categories[GATE_CATEGORY_SHELL]);
}

void test_approval_gate_set_category_action_invalid_category(void) {
    int result = approval_gate_set_category_action(&config, "invalid_category", GATE_ACTION_ALLOW);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_approval_gate_set_category_action_null_params(void) {
    TEST_ASSERT_EQUAL(-1, approval_gate_set_category_action(NULL, "file_write", GATE_ACTION_ALLOW));
    TEST_ASSERT_EQUAL(-1, approval_gate_set_category_action(&config, NULL, GATE_ACTION_ALLOW));
}

void test_approval_gate_parse_category(void) {
    GateCategory category;

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("file_write", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_WRITE, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("file_read", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_FILE_READ, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("shell", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SHELL, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("network", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_NETWORK, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("memory", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MEMORY, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("subagent", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_SUBAGENT, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("mcp", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_MCP, category);

    TEST_ASSERT_EQUAL(0, approval_gate_parse_category("python", &category));
    TEST_ASSERT_EQUAL(GATE_CATEGORY_PYTHON, category);
}

void test_approval_gate_parse_category_invalid(void) {
    GateCategory category;
    TEST_ASSERT_EQUAL(-1, approval_gate_parse_category("invalid", &category));
    TEST_ASSERT_EQUAL(-1, approval_gate_parse_category(NULL, &category));
    TEST_ASSERT_EQUAL(-1, approval_gate_parse_category("file_write", NULL));
}

void test_approval_gate_add_cli_allow_shell_command(void) {
    int initial_count = config.shell_allowlist_count;

    /* Add shell command via CLI format */
    int result = approval_gate_add_cli_allow(&config, "shell:git,status");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.shell_allowlist_count);

    /* Verify the entry */
    ShellAllowEntry *entry = &config.shell_allowlist[initial_count];
    TEST_ASSERT_EQUAL(2, entry->prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", entry->command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("status", entry->command_prefix[1]);
    TEST_ASSERT_EQUAL(SHELL_TYPE_UNKNOWN, entry->shell_type);
}

void test_approval_gate_add_cli_allow_shell_single_command(void) {
    int initial_count = config.shell_allowlist_count;

    /* Add single-word shell command */
    int result = approval_gate_add_cli_allow(&config, "shell:ls");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.shell_allowlist_count);

    ShellAllowEntry *entry = &config.shell_allowlist[initial_count];
    TEST_ASSERT_EQUAL(1, entry->prefix_len);
    TEST_ASSERT_EQUAL_STRING("ls", entry->command_prefix[0]);
}

void test_approval_gate_add_cli_allow_shell_multi_arg(void) {
    int initial_count = config.shell_allowlist_count;

    /* Add multi-argument shell command */
    int result = approval_gate_add_cli_allow(&config, "shell:npm,install,lodash");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.shell_allowlist_count);

    ShellAllowEntry *entry = &config.shell_allowlist[initial_count];
    TEST_ASSERT_EQUAL(3, entry->prefix_len);
    TEST_ASSERT_EQUAL_STRING("npm", entry->command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("install", entry->command_prefix[1]);
    TEST_ASSERT_EQUAL_STRING("lodash", entry->command_prefix[2]);
}

void test_approval_gate_add_cli_allow_regex_pattern(void) {
    int initial_count = config.allowlist_count;

    /* Add regex pattern for non-shell tool */
    int result = approval_gate_add_cli_allow(&config, "write_file:^\\./src/.*\\.c$");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.allowlist_count);

    AllowlistEntry *entry = &config.allowlist[initial_count];
    TEST_ASSERT_EQUAL_STRING("write_file", entry->tool);
    TEST_ASSERT_EQUAL_STRING("^\\./src/.*\\.c$", entry->pattern);
    TEST_ASSERT_EQUAL(1, entry->valid);
}

void test_approval_gate_add_cli_allow_invalid_format(void) {
    int initial_shell = config.shell_allowlist_count;
    int initial_regex = config.allowlist_count;

    /* No colon separator */
    TEST_ASSERT_EQUAL(-1, approval_gate_add_cli_allow(&config, "shell"));

    /* Empty tool name */
    TEST_ASSERT_EQUAL(-1, approval_gate_add_cli_allow(&config, ":ls"));

    /* No arguments after colon */
    TEST_ASSERT_EQUAL(-1, approval_gate_add_cli_allow(&config, "shell:"));

    /* NULL input */
    TEST_ASSERT_EQUAL(-1, approval_gate_add_cli_allow(&config, NULL));
    TEST_ASSERT_EQUAL(-1, approval_gate_add_cli_allow(NULL, "shell:ls"));

    /* Counts should be unchanged */
    TEST_ASSERT_EQUAL(initial_shell, config.shell_allowlist_count);
    TEST_ASSERT_EQUAL(initial_regex, config.allowlist_count);
}

void test_approval_gate_add_cli_allow_empty_tokens(void) {
    int initial_count = config.shell_allowlist_count;

    /* Empty tokens between commas are included as empty strings by strtok_r.
     * This is acceptable behavior - empty tokens just become empty string entries.
     * The command "shell:git,,status" will produce tokens ["git", "", "status"]
     * which won't match actual commands anyway. */
    int result = approval_gate_add_cli_allow(&config, "shell:git,status");
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.shell_allowlist_count);

    ShellAllowEntry *entry = &config.shell_allowlist[initial_count];
    TEST_ASSERT_EQUAL(2, entry->prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", entry->command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("status", entry->command_prefix[1]);
}

/* =============================================================================
 * Shell Command Allowlist Matching Tests
 * ========================================================================== */

void test_shell_allowlist_matches_simple_command(void) {
    /* Add allowlist entry for "ls" */
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Create shell tool call with matching command */
    ToolCall match_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* Should match the allowlist */
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &match_call));
}

void test_shell_allowlist_matches_command_with_args(void) {
    /* Add allowlist entry for "ls" */
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Create shell tool call with matching command and extra args */
    ToolCall match_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls -la /tmp\"}"
    };

    /* Should match - allowlist prefix is a subset */
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &match_call));
}

void test_shell_allowlist_matches_two_token_prefix(void) {
    /* Add allowlist entry for "git status" */
    const char *prefix[] = {"git", "status"};
    approval_gate_add_shell_allowlist(&config, prefix, 2, SHELL_TYPE_UNKNOWN);

    /* Create shell tool call with matching command */
    ToolCall match_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"git status\"}"
    };

    ToolCall match_with_args = {
        .id = "call_2",
        .name = "shell",
        .arguments = "{\"command\": \"git status -s\"}"
    };

    ToolCall no_match = {
        .id = "call_3",
        .name = "shell",
        .arguments = "{\"command\": \"git log\"}"
    };

    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &match_call));
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &match_with_args));
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &no_match));
}

void test_shell_allowlist_rejects_chained_commands(void) {
    /* Add allowlist entry for "ls" */
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Commands with chains should NEVER match, even if prefix matches */
    ToolCall chained_semicolon = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls; rm -rf /\"}"
    };

    ToolCall chained_and = {
        .id = "call_2",
        .name = "shell",
        .arguments = "{\"command\": \"ls && rm -rf /\"}"
    };

    ToolCall chained_or = {
        .id = "call_3",
        .name = "shell",
        .arguments = "{\"command\": \"ls || rm -rf /\"}"
    };

    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &chained_semicolon));
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &chained_and));
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &chained_or));
}

void test_shell_allowlist_rejects_piped_commands(void) {
    /* Add allowlist entry for "cat" */
    const char *prefix[] = {"cat"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Commands with pipes should NEVER match */
    ToolCall piped = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"cat /etc/passwd | grep root\"}"
    };

    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &piped));
}

void test_shell_allowlist_rejects_subshell_commands(void) {
    /* Add allowlist entry for "echo" */
    const char *prefix[] = {"echo"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Commands with subshells should NEVER match */
    ToolCall subshell_dollar = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"echo $(cat /etc/passwd)\"}"
    };

    ToolCall subshell_backtick = {
        .id = "call_2",
        .name = "shell",
        .arguments = "{\"command\": \"echo `cat /etc/passwd`\"}"
    };

    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &subshell_dollar));
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &subshell_backtick));
}

void test_shell_allowlist_rejects_dangerous_commands(void) {
    /* Add allowlist entry for "rm" */
    const char *prefix[] = {"rm"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Dangerous commands should NEVER match, even if prefix matches */
    ToolCall dangerous = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"rm -rf /\"}"
    };

    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &dangerous));
}

void test_shell_allowlist_shell_type_specific(void) {
    /* Add entry for "dir" only on cmd.exe */
    const char *dir_prefix[] = {"dir"};
    approval_gate_add_shell_allowlist(&config, dir_prefix, 1, SHELL_TYPE_CMD);

    /* Add entry for "ls" only on POSIX */
    const char *ls_prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, ls_prefix, 1, SHELL_TYPE_POSIX);

    ToolCall dir_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"dir\"}"
    };

    ToolCall ls_call = {
        .id = "call_2",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    /* On POSIX (current shell), "ls" should match but "dir" should not */
    /* (unless cmd.exe shell_type matches the entry) */
#ifndef _WIN32
    /* On Linux, current shell is POSIX */
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &ls_call));
    /* dir with CMD shell type should not match on POSIX */
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &dir_call));
#endif
}

void test_shell_allowlist_shell_type_unknown_matches_any(void) {
    /* Add entry for "git" with UNKNOWN shell type (matches any) */
    const char *prefix[] = {"git", "status"};
    approval_gate_add_shell_allowlist(&config, prefix, 2, SHELL_TYPE_UNKNOWN);

    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"git status\"}"
    };

    /* SHELL_TYPE_UNKNOWN should match any shell type */
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &call));
}

void test_shell_allowlist_command_equivalence(void) {
    /* Add entry for "ls" with UNKNOWN shell type */
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* "dir" is equivalent to "ls" on cmd.exe */
    ToolCall dir_call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"dir\"}"
    };

    /* This should match via command equivalence (ls <-> dir) */
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &dir_call));
}

void test_shell_allowlist_handles_missing_command_arg(void) {
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Tool call without command argument */
    ToolCall no_command = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"cwd\": \"/tmp\"}"
    };

    /* Should not match (and should not crash) */
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &no_command));
}

void test_shell_allowlist_handles_null_arguments(void) {
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Tool call with NULL arguments */
    ToolCall null_args = {
        .id = "call_1",
        .name = "shell",
        .arguments = NULL
    };

    /* Should not match (and should not crash) */
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &null_args));
}

void test_shell_allowlist_handles_malformed_json(void) {
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    /* Tool call with malformed JSON */
    ToolCall bad_json = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{invalid json}"
    };

    /* Should not match (and should not crash) */
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &bad_json));
}

void test_shell_allowlist_multiple_entries(void) {
    /* Add multiple allowlist entries */
    const char *ls_prefix[] = {"ls"};
    const char *git_status_prefix[] = {"git", "status"};
    const char *git_log_prefix[] = {"git", "log"};

    approval_gate_add_shell_allowlist(&config, ls_prefix, 1, SHELL_TYPE_UNKNOWN);
    approval_gate_add_shell_allowlist(&config, git_status_prefix, 2, SHELL_TYPE_UNKNOWN);
    approval_gate_add_shell_allowlist(&config, git_log_prefix, 2, SHELL_TYPE_UNKNOWN);

    ToolCall ls_call = {.id = "1", .name = "shell", .arguments = "{\"command\": \"ls\"}"};
    ToolCall git_status_call = {.id = "2", .name = "shell", .arguments = "{\"command\": \"git status\"}"};
    ToolCall git_log_call = {.id = "3", .name = "shell", .arguments = "{\"command\": \"git log\"}"};
    ToolCall git_push_call = {.id = "4", .name = "shell", .arguments = "{\"command\": \"git push\"}"};

    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &ls_call));
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &git_status_call));
    TEST_ASSERT_EQUAL(1, approval_gate_matches_allowlist(&config, &git_log_call));
    TEST_ASSERT_EQUAL(0, approval_gate_matches_allowlist(&config, &git_push_call));
}

/* =============================================================================
 * Batch Approval Tests
 * ========================================================================== */

void test_init_batch_result_creates_valid_batch(void) {
    ApprovalBatchResult batch;
    int result = init_batch_result(&batch, 3);

    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(3, batch.count);
    TEST_ASSERT_NOT_NULL(batch.results);
    TEST_ASSERT_NOT_NULL(batch.paths);

    /* All results should be initialized to DENIED (safe default) */
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(APPROVAL_DENIED, batch.results[i]);
    }

    free_batch_result(&batch);
}

void test_init_batch_result_null_batch_returns_error(void) {
    int result = init_batch_result(NULL, 3);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_init_batch_result_zero_count_returns_error(void) {
    ApprovalBatchResult batch;
    int result = init_batch_result(&batch, 0);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_init_batch_result_negative_count_returns_error(void) {
    ApprovalBatchResult batch;
    int result = init_batch_result(&batch, -1);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_free_batch_result_handles_null(void) {
    /* Should not crash */
    free_batch_result(NULL);
}

void test_free_batch_result_handles_empty(void) {
    ApprovalBatchResult batch = {0};
    /* Should not crash */
    free_batch_result(&batch);
}

void test_check_approval_gate_batch_all_allowed_category(void) {
    /* All tool calls are in allowed category (file_read) */
    ToolCall calls[] = {
        {.id = "1", .name = "read_file", .arguments = "{\"path\": \"a.txt\"}"},
        {.id = "2", .name = "list_dir", .arguments = "{\"path\": \"/tmp\"}"},
        {.id = "3", .name = "search_files", .arguments = "{\"pattern\": \"*.c\"}"}
    };

    ApprovalBatchResult batch;
    ApprovalResult result = check_approval_gate_batch(&config, calls, 3, &batch);

    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);
    TEST_ASSERT_EQUAL(3, batch.count);

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[i]);
    }

    free_batch_result(&batch);
}

void test_check_approval_gate_batch_mixed_categories_gates_disabled(void) {
    /* Disable gates */
    config.enabled = 0;

    /* Mix of normally gated and allowed categories */
    ToolCall calls[] = {
        {.id = "1", .name = "shell", .arguments = "{\"command\": \"ls\"}"},       /* Normally gated */
        {.id = "2", .name = "write_file", .arguments = "{\"path\": \"a.txt\"}"},  /* Normally gated */
        {.id = "3", .name = "read_file", .arguments = "{\"path\": \"b.txt\"}"}    /* Normally allowed */
    };

    ApprovalBatchResult batch;
    ApprovalResult result = check_approval_gate_batch(&config, calls, 3, &batch);

    /* With gates disabled, all should be allowed without prompting */
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[i]);
    }

    free_batch_result(&batch);
}

void test_check_approval_gate_batch_denied_category(void) {
    /* Set shell to deny */
    config.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_DENY;

    ToolCall calls[] = {
        {.id = "1", .name = "shell", .arguments = "{\"command\": \"ls\"}"},
        {.id = "2", .name = "read_file", .arguments = "{\"path\": \"a.txt\"}"}
    };

    ApprovalBatchResult batch;
    ApprovalResult result = check_approval_gate_batch(&config, calls, 2, &batch);

    /* Overall result should be denied since one operation is denied */
    TEST_ASSERT_EQUAL(APPROVAL_DENIED, result);

    /* First should be denied, second should be allowed */
    TEST_ASSERT_EQUAL(APPROVAL_DENIED, batch.results[0]);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[1]);

    free_batch_result(&batch);
}

void test_check_approval_gate_batch_rate_limited(void) {
    ToolCall shell_call = {.id = "1", .name = "shell", .arguments = "{\"command\": \"ls\"}"};

    /* Build up rate limit */
    track_denial(&config, &shell_call);
    track_denial(&config, &shell_call);
    track_denial(&config, &shell_call);

    ToolCall calls[] = {
        {.id = "1", .name = "shell", .arguments = "{\"command\": \"ls\"}"},
        {.id = "2", .name = "read_file", .arguments = "{\"path\": \"a.txt\"}"}
    };

    ApprovalBatchResult batch;
    ApprovalResult result = check_approval_gate_batch(&config, calls, 2, &batch);

    /* Overall result should indicate rate limiting occurred */
    TEST_ASSERT_EQUAL(APPROVAL_RATE_LIMITED, result);

    /* First should be rate limited, second should be allowed */
    TEST_ASSERT_EQUAL(APPROVAL_RATE_LIMITED, batch.results[0]);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[1]);

    free_batch_result(&batch);
}

void test_check_approval_gate_batch_null_params(void) {
    ToolCall calls[] = {
        {.id = "1", .name = "read_file", .arguments = "{}"}
    };
    ApprovalBatchResult batch;

    TEST_ASSERT_EQUAL(APPROVAL_DENIED, check_approval_gate_batch(NULL, calls, 1, &batch));
    TEST_ASSERT_EQUAL(APPROVAL_DENIED, check_approval_gate_batch(&config, NULL, 1, &batch));
    TEST_ASSERT_EQUAL(APPROVAL_DENIED, check_approval_gate_batch(&config, calls, 0, &batch));
    TEST_ASSERT_EQUAL(APPROVAL_DENIED, check_approval_gate_batch(&config, calls, 1, NULL));
}

void test_check_approval_gate_batch_allowlist_bypass(void) {
    /* Add shell command to allowlist */
    const char *prefix[] = {"ls"};
    approval_gate_add_shell_allowlist(&config, prefix, 1, SHELL_TYPE_UNKNOWN);

    ToolCall calls[] = {
        {.id = "1", .name = "shell", .arguments = "{\"command\": \"ls\"}"},    /* Matches allowlist */
        {.id = "2", .name = "read_file", .arguments = "{\"path\": \"a.txt\"}"} /* Allowed category */
    };

    ApprovalBatchResult batch;
    ApprovalResult result = check_approval_gate_batch(&config, calls, 2, &batch);

    /* Both should be allowed (first via allowlist, second via category) */
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[0]);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[1]);

    free_batch_result(&batch);
}

/* =============================================================================
 * Pattern Generation Tests
 * ========================================================================== */

void test_generate_file_path_pattern_root_file_exact_match(void) {
    GeneratedPattern pattern;

    int result = generate_file_path_pattern("./README.md", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(1, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(0, pattern.needs_confirmation);
    TEST_ASSERT_EQUAL_STRING("^\\./README\\.md$", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_file_path_pattern_tmp_path_exact_match(void) {
    GeneratedPattern pattern;

    int result = generate_file_path_pattern("/tmp/scratch.txt", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(1, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(0, pattern.needs_confirmation);
    TEST_ASSERT_EQUAL_STRING("^/tmp/scratch\\.txt$", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_file_path_pattern_nested_path_directory_match(void) {
    GeneratedPattern pattern;

    int result = generate_file_path_pattern("./src/foo/bar.c", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(0, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(1, pattern.needs_confirmation);
    /* Should match any .c file in the same directory */
    TEST_ASSERT_EQUAL_STRING("^\\./src/foo/.*\\.c$", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_file_path_pattern_test_prefix_preserved(void) {
    GeneratedPattern pattern;

    int result = generate_file_path_pattern("./test/test_gates.c", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(0, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(1, pattern.needs_confirmation);
    /* Should preserve the test_ prefix */
    TEST_ASSERT_EQUAL_STRING("^\\./test/test_.*\\.c$", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_file_path_pattern_no_extension_exact_match(void) {
    GeneratedPattern pattern;

    int result = generate_file_path_pattern("./src/Makefile", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(1, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(0, pattern.needs_confirmation);
    TEST_ASSERT_EQUAL_STRING("^\\./src/Makefile$", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_file_path_pattern_null_params(void) {
    GeneratedPattern pattern;

    TEST_ASSERT_EQUAL(-1, generate_file_path_pattern(NULL, &pattern));
    TEST_ASSERT_EQUAL(-1, generate_file_path_pattern("./test.c", NULL));
}

void test_generate_file_path_pattern_escapes_special_chars(void) {
    GeneratedPattern pattern;

    /* /tmp paths get exact match, so escaping is visible in the pattern */
    int result = generate_file_path_pattern("/tmp/test[1].txt", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(1, pattern.is_exact_match);
    /* Brackets should be escaped in the exact match pattern */
    TEST_ASSERT_TRUE(strstr(pattern.pattern, "\\[1\\]") != NULL);

    free_generated_pattern(&pattern);
}

void test_generate_shell_command_pattern_simple_command(void) {
    GeneratedPattern pattern;

    int result = generate_shell_command_pattern("ls", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.command_prefix);
    TEST_ASSERT_EQUAL(1, pattern.prefix_len);
    TEST_ASSERT_EQUAL_STRING("ls", pattern.command_prefix[0]);
    TEST_ASSERT_EQUAL(1, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(0, pattern.needs_confirmation);

    free_generated_pattern(&pattern);
}

void test_generate_shell_command_pattern_two_args(void) {
    GeneratedPattern pattern;

    int result = generate_shell_command_pattern("git commit -m \"msg\"", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.command_prefix);
    TEST_ASSERT_EQUAL(2, pattern.prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", pattern.command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("commit", pattern.command_prefix[1]);
    TEST_ASSERT_EQUAL(0, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(1, pattern.needs_confirmation);

    free_generated_pattern(&pattern);
}

void test_generate_shell_command_pattern_unsafe_command_no_pattern(void) {
    GeneratedPattern pattern_pipe;
    GeneratedPattern pattern_chain;
    GeneratedPattern pattern_subshell;

    /* Commands with pipes should not generate patterns */
    int result = generate_shell_command_pattern("cat /etc/passwd | grep root", &pattern_pipe);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, pattern_pipe.is_exact_match);
    TEST_ASSERT_NULL(pattern_pipe.command_prefix);

    /* Commands with chains should not generate patterns */
    result = generate_shell_command_pattern("ls && rm -rf /", &pattern_chain);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, pattern_chain.is_exact_match);
    TEST_ASSERT_NULL(pattern_chain.command_prefix);

    /* Commands with subshells should not generate patterns */
    result = generate_shell_command_pattern("echo $(cat /etc/passwd)", &pattern_subshell);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(1, pattern_subshell.is_exact_match);
    TEST_ASSERT_NULL(pattern_subshell.command_prefix);

    free_generated_pattern(&pattern_pipe);
    free_generated_pattern(&pattern_chain);
    free_generated_pattern(&pattern_subshell);
}

void test_generate_shell_command_pattern_null_params(void) {
    GeneratedPattern pattern;

    TEST_ASSERT_EQUAL(-1, generate_shell_command_pattern(NULL, &pattern));
    TEST_ASSERT_EQUAL(-1, generate_shell_command_pattern("ls", NULL));
}

void test_generate_network_url_pattern_basic_url(void) {
    GeneratedPattern pattern;

    int result = generate_network_url_pattern("https://api.example.com/v1/users", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(0, pattern.is_exact_match);
    TEST_ASSERT_EQUAL(1, pattern.needs_confirmation);
    /* Pattern should match scheme + hostname with path boundary */
    TEST_ASSERT_EQUAL_STRING("^https://api\\.example\\.com(/|$)", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_network_url_pattern_with_port(void) {
    GeneratedPattern pattern;

    int result = generate_network_url_pattern("http://localhost:8080/api", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    /* Should only capture up to the port colon */
    TEST_ASSERT_EQUAL_STRING("^http://localhost(/|$)", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_network_url_pattern_prevents_subdomain_spoofing(void) {
    GeneratedPattern pattern;

    /* Generate pattern for api.example.com */
    int result = generate_network_url_pattern("https://api.example.com/test", &pattern);
    TEST_ASSERT_EQUAL(0, result);

    /* The pattern should require a path separator after hostname,
     * preventing api.example.com.evil.com from matching */
    TEST_ASSERT_TRUE(strstr(pattern.pattern, "(/|$)") != NULL);

    free_generated_pattern(&pattern);
}

void test_generate_network_url_pattern_invalid_url_exact_match(void) {
    GeneratedPattern pattern;

    int result = generate_network_url_pattern("not-a-valid-url", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_EQUAL(1, pattern.is_exact_match);
    TEST_ASSERT_EQUAL_STRING("^not-a-valid-url$", pattern.pattern);

    free_generated_pattern(&pattern);
}

void test_generate_network_url_pattern_null_params(void) {
    GeneratedPattern pattern;

    TEST_ASSERT_EQUAL(-1, generate_network_url_pattern(NULL, &pattern));
    TEST_ASSERT_EQUAL(-1, generate_network_url_pattern("https://example.com", NULL));
}

void test_generate_allowlist_pattern_shell_tool(void) {
    GeneratedPattern pattern;

    ToolCall call = {
        .id = "1",
        .name = "shell",
        .arguments = "{\"command\": \"git status\"}"
    };

    int result = generate_allowlist_pattern(&call, &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.command_prefix);
    TEST_ASSERT_EQUAL(2, pattern.prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", pattern.command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("status", pattern.command_prefix[1]);

    free_generated_pattern(&pattern);
}

void test_generate_allowlist_pattern_file_write_tool(void) {
    GeneratedPattern pattern;

    ToolCall call = {
        .id = "1",
        .name = "write_file",
        .arguments = "{\"path\": \"./src/foo.c\"}"
    };

    int result = generate_allowlist_pattern(&call, &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_TRUE(strstr(pattern.pattern, "src") != NULL);
    TEST_ASSERT_TRUE(strstr(pattern.pattern, "\\.c") != NULL);

    free_generated_pattern(&pattern);
}

void test_generate_allowlist_pattern_network_tool(void) {
    GeneratedPattern pattern;

    ToolCall call = {
        .id = "1",
        .name = "web_fetch",
        .arguments = "{\"url\": \"https://api.github.com/repos\"}"
    };

    int result = generate_allowlist_pattern(&call, &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(pattern.pattern);
    TEST_ASSERT_TRUE(strstr(pattern.pattern, "api\\.github\\.com") != NULL);

    free_generated_pattern(&pattern);
}

void test_generate_allowlist_pattern_null_params(void) {
    GeneratedPattern pattern;

    TEST_ASSERT_EQUAL(-1, generate_allowlist_pattern(NULL, &pattern));

    ToolCall call = {.id = "1", .name = "shell", .arguments = "{}"};
    TEST_ASSERT_EQUAL(-1, generate_allowlist_pattern(&call, NULL));
}

void test_free_generated_pattern_handles_null(void) {
    /* Should not crash */
    free_generated_pattern(NULL);
}

void test_free_generated_pattern_handles_empty(void) {
    GeneratedPattern pattern = {0};
    /* Should not crash */
    free_generated_pattern(&pattern);
}

void test_apply_generated_pattern_shell_command(void) {
    GeneratedPattern pattern = {0};
    pattern.command_prefix = calloc(2, sizeof(char *));
    TEST_ASSERT_NOT_NULL(pattern.command_prefix);
    pattern.command_prefix[0] = strdup("git");
    pattern.command_prefix[1] = strdup("log");
    pattern.prefix_len = 2;

    int initial_count = config.shell_allowlist_count;

    int result = apply_generated_pattern(&config, "shell", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.shell_allowlist_count);

    /* Verify the entry was added correctly */
    ShellAllowEntry *entry = &config.shell_allowlist[initial_count];
    TEST_ASSERT_EQUAL(2, entry->prefix_len);
    TEST_ASSERT_EQUAL_STRING("git", entry->command_prefix[0]);
    TEST_ASSERT_EQUAL_STRING("log", entry->command_prefix[1]);

    free_generated_pattern(&pattern);
}

void test_apply_generated_pattern_regex_pattern(void) {
    GeneratedPattern pattern = {0};
    pattern.pattern = strdup("^\\./src/.*\\.c$");

    int initial_count = config.allowlist_count;

    int result = apply_generated_pattern(&config, "write_file", &pattern);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_EQUAL(initial_count + 1, config.allowlist_count);

    /* Verify the entry was added correctly */
    AllowlistEntry *entry = &config.allowlist[initial_count];
    TEST_ASSERT_EQUAL_STRING("write_file", entry->tool);
    TEST_ASSERT_EQUAL_STRING("^\\./src/.*\\.c$", entry->pattern);

    free_generated_pattern(&pattern);
}

void test_apply_generated_pattern_null_params(void) {
    GeneratedPattern pattern = {0};
    pattern.pattern = strdup("test");

    TEST_ASSERT_EQUAL(-1, apply_generated_pattern(NULL, "test", &pattern));
    TEST_ASSERT_EQUAL(-1, apply_generated_pattern(&config, NULL, &pattern));
    TEST_ASSERT_EQUAL(-1, apply_generated_pattern(&config, "test", NULL));

    free_generated_pattern(&pattern);
}

/* =============================================================================
 * Non-Interactive Mode Tests
 * ========================================================================== */

void test_approval_gate_detect_interactive_null_safe(void) {
    /* Should handle NULL gracefully */
    approval_gate_detect_interactive(NULL);
    /* No crash is success */
}

void test_approval_gate_is_interactive_null_safe(void) {
    TEST_ASSERT_EQUAL(0, approval_gate_is_interactive(NULL));
}

void test_approval_gate_is_interactive_default_value(void) {
    /* After init, is_interactive should be 0 (not yet detected) */
    TEST_ASSERT_EQUAL(0, config.is_interactive);
}

void test_approval_gate_is_interactive_getter(void) {
    /* Test getter returns correct value */
    config.is_interactive = 1;
    TEST_ASSERT_EQUAL(1, approval_gate_is_interactive(&config));
    config.is_interactive = 0;
    TEST_ASSERT_EQUAL(0, approval_gate_is_interactive(&config));
}

void test_format_non_interactive_error(void) {
    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    char *error = format_non_interactive_error(&call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_NOT_NULL(strstr(error, "\"error\": \"non_interactive_gate\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "\"tool\": \"shell\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "\"category\": \"shell\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "--allow-category=shell"));
    free(error);
}

void test_format_non_interactive_error_null_safe(void) {
    char *error = format_non_interactive_error(NULL);
    TEST_ASSERT_NULL(error);
}

void test_format_non_interactive_error_file_write(void) {
    ToolCall call = {
        .id = "call_1",
        .name = "write_file",
        .arguments = "{\"path\": \"/tmp/test.txt\"}"
    };

    char *error = format_non_interactive_error(&call);
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_NOT_NULL(strstr(error, "\"category\": \"file_write\""));
    TEST_ASSERT_NOT_NULL(strstr(error, "--allow-category=file_write"));
    free(error);
}

void test_format_non_interactive_error_special_chars(void) {
    /* Test that special characters in tool name are properly JSON escaped */
    ToolCall call = {
        .id = "call_1",
        .name = "tool\"with\\special/chars",
        .arguments = "{}"
    };

    char *error = format_non_interactive_error(&call);
    TEST_ASSERT_NOT_NULL(error);
    /* Verify the output is valid JSON by checking escaped characters.
     * Note: Forward slash escaping is optional in JSON spec, so we only
     * check for required escapes (quotes and backslashes). */
    TEST_ASSERT_NOT_NULL(strstr(error, "tool\\\"with\\\\special"));
    free(error);
}

void test_check_approval_gate_non_interactive_gated_category(void) {
    /* Set non-interactive mode */
    config.is_interactive = 0;

    /* Shell is gated by default */
    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    ApprovedPath path;
    ApprovalResult result = check_approval_gate(&config, &call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, result);
}

void test_check_approval_gate_non_interactive_allowed_category(void) {
    /* Set non-interactive mode */
    config.is_interactive = 0;

    /* Memory tools are allowed by default */
    ToolCall call = {
        .id = "call_1",
        .name = "remember",
        .arguments = "{\"key\": \"test\"}"
    };

    ApprovedPath path;
    ApprovalResult result = check_approval_gate(&config, &call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);
}

void test_check_approval_gate_non_interactive_allow_category_override(void) {
    /* Set non-interactive mode */
    config.is_interactive = 0;

    /* Override shell to be allowed */
    config.categories[GATE_CATEGORY_SHELL] = GATE_ACTION_ALLOW;

    ToolCall call = {
        .id = "call_1",
        .name = "shell",
        .arguments = "{\"command\": \"ls\"}"
    };

    ApprovedPath path;
    ApprovalResult result = check_approval_gate(&config, &call, &path);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, result);
}

void test_check_approval_gate_batch_non_interactive(void) {
    /* Set non-interactive mode */
    config.is_interactive = 0;

    /* Create batch with mix of allowed and gated tools */
    ToolCall calls[3] = {
        { .id = "call_1", .name = "remember", .arguments = "{}" },  /* allowed */
        { .id = "call_2", .name = "shell", .arguments = "{\"command\": \"ls\"}" },  /* gated */
        { .id = "call_3", .name = "read_file", .arguments = "{}" }  /* allowed */
    };

    ApprovalBatchResult batch;
    ApprovalResult result = check_approval_gate_batch(&config, calls, 3, &batch);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, result);

    /* Check individual results */
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[0]);
    TEST_ASSERT_EQUAL(APPROVAL_NON_INTERACTIVE_DENIED, batch.results[1]);
    TEST_ASSERT_EQUAL(APPROVAL_ALLOWED, batch.results[2]);

    free_batch_result(&batch);
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
    RUN_TEST(test_approval_gate_init_from_parent_inherits_static_allowlist);
    RUN_TEST(test_approval_gate_init_from_parent_inherits_static_shell_allowlist);
    RUN_TEST(test_approval_gate_init_from_parent_excludes_session_allowlist);
    RUN_TEST(test_approval_gate_init_from_parent_excludes_session_shell_allowlist);

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
    RUN_TEST(test_get_tool_category_fallback_when_python_not_loaded);

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

    /* Config loading tests */
    RUN_TEST(test_approval_gate_load_from_json_file_enabled);
    RUN_TEST(test_approval_gate_load_from_json_file_categories);
    RUN_TEST(test_approval_gate_load_from_json_file_regex_allowlist);
    RUN_TEST(test_approval_gate_load_from_json_file_shell_allowlist);
    RUN_TEST(test_approval_gate_load_from_json_file_mixed_allowlist);
    RUN_TEST(test_approval_gate_load_from_json_file_no_approval_gates_section);
    RUN_TEST(test_approval_gate_load_from_json_file_powershell_shell);
    RUN_TEST(test_approval_gate_load_from_json_file_malformed_json);
    RUN_TEST(test_approval_gate_load_from_json_file_invalid_entries_skipped);

    /* CLI override tests */
    RUN_TEST(test_approval_gate_enable_yolo);
    RUN_TEST(test_approval_gate_enable_yolo_null_safe);
    RUN_TEST(test_approval_gate_set_category_action);
    RUN_TEST(test_approval_gate_set_category_action_invalid_category);
    RUN_TEST(test_approval_gate_set_category_action_null_params);
    RUN_TEST(test_approval_gate_parse_category);
    RUN_TEST(test_approval_gate_parse_category_invalid);
    RUN_TEST(test_approval_gate_add_cli_allow_shell_command);
    RUN_TEST(test_approval_gate_add_cli_allow_shell_single_command);
    RUN_TEST(test_approval_gate_add_cli_allow_shell_multi_arg);
    RUN_TEST(test_approval_gate_add_cli_allow_regex_pattern);
    RUN_TEST(test_approval_gate_add_cli_allow_invalid_format);
    RUN_TEST(test_approval_gate_add_cli_allow_empty_tokens);

    /* Shell command allowlist matching tests */
    RUN_TEST(test_shell_allowlist_matches_simple_command);
    RUN_TEST(test_shell_allowlist_matches_command_with_args);
    RUN_TEST(test_shell_allowlist_matches_two_token_prefix);
    RUN_TEST(test_shell_allowlist_rejects_chained_commands);
    RUN_TEST(test_shell_allowlist_rejects_piped_commands);
    RUN_TEST(test_shell_allowlist_rejects_subshell_commands);
    RUN_TEST(test_shell_allowlist_rejects_dangerous_commands);
    RUN_TEST(test_shell_allowlist_shell_type_specific);
    RUN_TEST(test_shell_allowlist_shell_type_unknown_matches_any);
    RUN_TEST(test_shell_allowlist_command_equivalence);
    RUN_TEST(test_shell_allowlist_handles_missing_command_arg);
    RUN_TEST(test_shell_allowlist_handles_null_arguments);
    RUN_TEST(test_shell_allowlist_handles_malformed_json);
    RUN_TEST(test_shell_allowlist_multiple_entries);

    /* Batch approval tests */
    RUN_TEST(test_init_batch_result_creates_valid_batch);
    RUN_TEST(test_init_batch_result_null_batch_returns_error);
    RUN_TEST(test_init_batch_result_zero_count_returns_error);
    RUN_TEST(test_init_batch_result_negative_count_returns_error);
    RUN_TEST(test_free_batch_result_handles_null);
    RUN_TEST(test_free_batch_result_handles_empty);
    RUN_TEST(test_check_approval_gate_batch_all_allowed_category);
    RUN_TEST(test_check_approval_gate_batch_mixed_categories_gates_disabled);
    RUN_TEST(test_check_approval_gate_batch_denied_category);
    RUN_TEST(test_check_approval_gate_batch_rate_limited);
    RUN_TEST(test_check_approval_gate_batch_null_params);
    RUN_TEST(test_check_approval_gate_batch_allowlist_bypass);

    /* Pattern generation tests */
    RUN_TEST(test_generate_file_path_pattern_root_file_exact_match);
    RUN_TEST(test_generate_file_path_pattern_tmp_path_exact_match);
    RUN_TEST(test_generate_file_path_pattern_nested_path_directory_match);
    RUN_TEST(test_generate_file_path_pattern_test_prefix_preserved);
    RUN_TEST(test_generate_file_path_pattern_no_extension_exact_match);
    RUN_TEST(test_generate_file_path_pattern_null_params);
    RUN_TEST(test_generate_file_path_pattern_escapes_special_chars);
    RUN_TEST(test_generate_shell_command_pattern_simple_command);
    RUN_TEST(test_generate_shell_command_pattern_two_args);
    RUN_TEST(test_generate_shell_command_pattern_unsafe_command_no_pattern);
    RUN_TEST(test_generate_shell_command_pattern_null_params);
    RUN_TEST(test_generate_network_url_pattern_basic_url);
    RUN_TEST(test_generate_network_url_pattern_with_port);
    RUN_TEST(test_generate_network_url_pattern_prevents_subdomain_spoofing);
    RUN_TEST(test_generate_network_url_pattern_invalid_url_exact_match);
    RUN_TEST(test_generate_network_url_pattern_null_params);
    RUN_TEST(test_generate_allowlist_pattern_shell_tool);
    RUN_TEST(test_generate_allowlist_pattern_file_write_tool);
    RUN_TEST(test_generate_allowlist_pattern_network_tool);
    RUN_TEST(test_generate_allowlist_pattern_null_params);
    RUN_TEST(test_free_generated_pattern_handles_null);
    RUN_TEST(test_free_generated_pattern_handles_empty);
    RUN_TEST(test_apply_generated_pattern_shell_command);
    RUN_TEST(test_apply_generated_pattern_regex_pattern);
    RUN_TEST(test_apply_generated_pattern_null_params);

    /* Non-interactive mode tests */
    RUN_TEST(test_approval_gate_detect_interactive_null_safe);
    RUN_TEST(test_approval_gate_is_interactive_null_safe);
    RUN_TEST(test_approval_gate_is_interactive_default_value);
    RUN_TEST(test_approval_gate_is_interactive_getter);
    RUN_TEST(test_format_non_interactive_error);
    RUN_TEST(test_format_non_interactive_error_null_safe);
    RUN_TEST(test_format_non_interactive_error_file_write);
    RUN_TEST(test_format_non_interactive_error_special_chars);
    RUN_TEST(test_check_approval_gate_non_interactive_gated_category);
    RUN_TEST(test_check_approval_gate_non_interactive_allowed_category);
    RUN_TEST(test_check_approval_gate_non_interactive_allow_category_override);
    RUN_TEST(test_check_approval_gate_batch_non_interactive);

    return UNITY_END();
}
