/**
 * Unit tests for Windows cmd.exe shell command parsing
 *
 * Tests cmd.exe-specific tokenization, double-quote handling,
 * metacharacter detection, and dangerous pattern matching.
 *
 * These tests verify the implementation of parse_cmd_shell() with
 * proper cmd.exe semantics:
 * - Only double quotes are string delimiters (single quotes are literal)
 * - Metacharacters: & | < > ^ %
 * - & is unconditional separator (like ; in POSIX)
 * - ^ is the escape character
 * - %VAR% is variable expansion
 */

#include "unity.h"
#include "shell_parser.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * Basic Tokenization Tests
 * ========================================================================== */

void test_cmd_parse_simple_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_CMD, cmd->shell_type);
    TEST_ASSERT_EQUAL_INT(1, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("dir", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    TEST_ASSERT_EQUAL_INT(0, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_parse_command_with_arguments(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir /w /p", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("dir", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("/w", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("/p", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_cmd_parse_empty_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_cmd_parse_null_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type(NULL, SHELL_TYPE_CMD);
    TEST_ASSERT_NULL(cmd);
}

void test_cmd_parse_multiple_spaces(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("type   file.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("type", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("file.txt", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Double Quote Handling (cmd.exe only uses double quotes)
 * ========================================================================== */

void test_cmd_double_quoted_argument(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"hello world\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("hello world", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_cmd_double_quoted_with_path(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("cd \"C:\\Program Files\\App\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("cd", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("C:\\Program Files\\App", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_cmd_single_quotes_are_literal(void) {
    /* In cmd.exe, single quotes are NOT string delimiters */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo 'hello world'", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Single quotes are literal, so "'hello" and "world'" are separate tokens */
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("'hello", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("world'", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_cmd_empty_double_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"\" arg", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("arg", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_cmd_adjacent_double_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"hello\"\"world\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("helloworld", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: & (Command Separator)
 * ========================================================================== */

void test_cmd_ampersand_chain_detected(void) {
    /* & is unconditional command separator in cmd.exe */
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir & echo done", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_double_ampersand_chain_detected(void) {
    /* && is conditional AND */
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir && echo success", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_double_pipe_chain_detected(void) {
    /* || is conditional OR */
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir || echo failed", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_ampersand_quoted_not_chain(void) {
    /* & inside double quotes should not be detected as separator */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"foo & bar\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: | (Pipe)
 * ========================================================================== */

void test_cmd_pipe_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir | findstr foo", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

void test_cmd_pipe_quoted_not_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"|\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: < > (Redirection)
 * ========================================================================== */

void test_cmd_redirect_output_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir > output.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_cmd_redirect_append_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo hello >> log.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_cmd_redirect_input_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("more < file.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_cmd_redirect_quoted_not_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"<>\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: ^ (Escape Character)
 * ========================================================================== */

void test_cmd_caret_escape_detected(void) {
    /* ^ is escape character in cmd.exe - can escape metacharacters */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo ^&", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Caret presence makes command unsafe for matching */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_caret_escape_pipe(void) {
    /* ^| escapes the pipe */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo hello^|world", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Caret presence should flag as unsafe */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_caret_at_end(void) {
    /* Trailing caret (line continuation) */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo hello^", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_caret_quoted_not_escape(void) {
    /* ^ inside quotes is literal */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"hello^world\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: % (Variable Expansion)
 * ========================================================================== */

void test_cmd_percent_variable_detected(void) {
    /* %VAR% is variable expansion */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo %PATH%", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);  /* Variable expansion treated as subshell for safety */
    free_parsed_shell_command(cmd);
}

void test_cmd_percent_in_for_loop(void) {
    /* %i is used in FOR loops */
    ParsedShellCommand *cmd = parse_shell_command_for_type("for %i in (*) do echo %i", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_cmd_percent_quoted_detected(void) {
    /* % variables still expand inside double quotes in cmd.exe */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"%PATH%\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Variable expansion inside quotes still occurs in cmd.exe, so should be flagged */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_cmd_single_percent_flagged_conservatively(void) {
    /* A single % is still flagged for safety (conservative approach) */
    /* This prevents attacks using pseudo-variables like %cd% */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo 50%", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Conservative: flag any % character to prevent variable injection */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Unbalanced Quotes
 * ========================================================================== */

void test_cmd_unbalanced_quotes_flagged(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"unclosed", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Unbalanced quotes should make command unsafe for matching */
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Dangerous Patterns (cmd.exe specific)
 * ========================================================================== */

void test_cmd_del_recursive_dangerous(void) {
    /* del /s is flagged as dangerous */
    ParsedShellCommand *cmd = parse_shell_command_for_type("del /s /q *.*", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_format_dangerous(void) {
    /* format command is dangerous */
    ParsedShellCommand *cmd = parse_shell_command_for_type("format c:", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_diskpart_dangerous(void) {
    /* diskpart is dangerous */
    ParsedShellCommand *cmd = parse_shell_command_for_type("diskpart /s script.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_rd_recursive_dangerous(void) {
    /* rd /s is dangerous (recursive directory removal) */
    ParsedShellCommand *cmd = parse_shell_command_for_type("rd /s /q C:\\temp", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_powershell_invocation_dangerous(void) {
    /* Invoking PowerShell from cmd.exe is dangerous */
    ParsedShellCommand *cmd = parse_shell_command_for_type("powershell -ExecutionPolicy Bypass -File script.ps1", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_reg_delete_dangerous(void) {
    /* Registry deletion is dangerous */
    ParsedShellCommand *cmd = parse_shell_command_for_type("reg delete HKCU\\Software\\Test", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_cmd_safe_command_not_dangerous(void) {
    /* Safe commands should not be flagged */
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir /w", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Allowlist Matching
 * ========================================================================== */

void test_cmd_simple_match(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir /w", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"dir"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_cmd_prefix_match(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status -s", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_cmd_no_match_with_chain(void) {
    /* Commands with chains should never match */
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir & del *.*", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"dir"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_cmd_no_match_with_pipe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir | findstr foo", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"dir"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_cmd_no_match_with_variable(void) {
    /* Commands with variable expansion should not match */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo %PATH%", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"echo"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Safety Check
 * ========================================================================== */

void test_cmd_safe_simple_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_cmd_not_safe_with_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir & echo done", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_cmd_not_safe_with_variable(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo %USERPROFILE%", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_cmd_not_safe_with_caret(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo ^&", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

void test_cmd_get_base_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir /w /p", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("dir", shell_command_get_base(cmd));
    free_parsed_shell_command(cmd);
}

void test_cmd_copy_command(void) {
    ParsedShellCommand *orig = parse_shell_command_for_type("type file.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(orig);

    ParsedShellCommand *copy = copy_parsed_shell_command(orig);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_CMD, copy->shell_type);
    TEST_ASSERT_EQUAL_INT(orig->token_count, copy->token_count);
    TEST_ASSERT_EQUAL_STRING(orig->tokens[0], copy->tokens[0]);

    free_parsed_shell_command(orig);
    free_parsed_shell_command(copy);
}

/* ============================================================================
 * Edge Cases
 * ========================================================================== */

void test_cmd_whitespace_only(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("   \t  ", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_cmd_path_with_backslashes(void) {
    /* Windows paths use backslashes */
    ParsedShellCommand *cmd = parse_shell_command_for_type("type C:\\Users\\test\\file.txt", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("type", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("C:\\Users\\test\\file.txt", cmd->tokens[1]);
    /* Backslash is NOT an escape character in cmd.exe */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_quoted_path_with_spaces(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("\"C:\\Program Files\\app.exe\" arg1", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("C:\\Program Files\\app.exe", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("arg1", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_cmd_escaped_quote_inside_string(void) {
    /*
     * In cmd.exe, "" inside quotes represents a literal quote.
     * The current parser doesn't fully handle this, but it should
     * still parse safely without crashing or detecting false positives.
     */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"hello \"\"world\"\"\"", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Parser sees this as valid (no crash, no false chain detection) */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_cmd_mixed_metacharacters(void) {
    /* Command with multiple metacharacter types */
    ParsedShellCommand *cmd = parse_shell_command_for_type("dir ^& echo | findstr test", SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Should detect both chain (from ^) and pipe */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Test Runner
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Basic Tokenization */
    RUN_TEST(test_cmd_parse_simple_command);
    RUN_TEST(test_cmd_parse_command_with_arguments);
    RUN_TEST(test_cmd_parse_empty_command);
    RUN_TEST(test_cmd_parse_null_command);
    RUN_TEST(test_cmd_parse_multiple_spaces);

    /* Double Quote Handling */
    RUN_TEST(test_cmd_double_quoted_argument);
    RUN_TEST(test_cmd_double_quoted_with_path);
    RUN_TEST(test_cmd_single_quotes_are_literal);
    RUN_TEST(test_cmd_empty_double_quotes);
    RUN_TEST(test_cmd_adjacent_double_quotes);

    /* Ampersand Chain Detection */
    RUN_TEST(test_cmd_ampersand_chain_detected);
    RUN_TEST(test_cmd_double_ampersand_chain_detected);
    RUN_TEST(test_cmd_double_pipe_chain_detected);
    RUN_TEST(test_cmd_ampersand_quoted_not_chain);

    /* Pipe Detection */
    RUN_TEST(test_cmd_pipe_detected);
    RUN_TEST(test_cmd_pipe_quoted_not_detected);

    /* Redirect Detection */
    RUN_TEST(test_cmd_redirect_output_detected);
    RUN_TEST(test_cmd_redirect_append_detected);
    RUN_TEST(test_cmd_redirect_input_detected);
    RUN_TEST(test_cmd_redirect_quoted_not_detected);

    /* Caret Escape Detection */
    RUN_TEST(test_cmd_caret_escape_detected);
    RUN_TEST(test_cmd_caret_escape_pipe);
    RUN_TEST(test_cmd_caret_at_end);
    RUN_TEST(test_cmd_caret_quoted_not_escape);

    /* Variable Expansion Detection */
    RUN_TEST(test_cmd_percent_variable_detected);
    RUN_TEST(test_cmd_percent_in_for_loop);
    RUN_TEST(test_cmd_percent_quoted_detected);
    RUN_TEST(test_cmd_single_percent_flagged_conservatively);

    /* Unbalanced Quotes */
    RUN_TEST(test_cmd_unbalanced_quotes_flagged);

    /* Dangerous Patterns */
    RUN_TEST(test_cmd_del_recursive_dangerous);
    RUN_TEST(test_cmd_format_dangerous);
    RUN_TEST(test_cmd_diskpart_dangerous);
    RUN_TEST(test_cmd_rd_recursive_dangerous);
    RUN_TEST(test_cmd_powershell_invocation_dangerous);
    RUN_TEST(test_cmd_reg_delete_dangerous);
    RUN_TEST(test_cmd_safe_command_not_dangerous);

    /* Allowlist Matching */
    RUN_TEST(test_cmd_simple_match);
    RUN_TEST(test_cmd_prefix_match);
    RUN_TEST(test_cmd_no_match_with_chain);
    RUN_TEST(test_cmd_no_match_with_pipe);
    RUN_TEST(test_cmd_no_match_with_variable);

    /* Safety Check */
    RUN_TEST(test_cmd_safe_simple_command);
    RUN_TEST(test_cmd_not_safe_with_chain);
    RUN_TEST(test_cmd_not_safe_with_variable);
    RUN_TEST(test_cmd_not_safe_with_caret);

    /* Utility Functions */
    RUN_TEST(test_cmd_get_base_command);
    RUN_TEST(test_cmd_copy_command);

    /* Edge Cases */
    RUN_TEST(test_cmd_whitespace_only);
    RUN_TEST(test_cmd_path_with_backslashes);
    RUN_TEST(test_cmd_quoted_path_with_spaces);
    RUN_TEST(test_cmd_escaped_quote_inside_string);
    RUN_TEST(test_cmd_mixed_metacharacters);

    return UNITY_END();
}
