#ifndef SHELL_RULES_H
#define SHELL_RULES_H

#include "shell_parser.h"

/**
 * Shell allow rules created by pressing "a" at the approval prompt.
 *
 * A rule is the command name plus, for tools that have subcommands (git,
 * docker, make, ...), the subcommand. Matching ignores flag order and global
 * options, so a rule saved from "git -C lib show HEAD" still matches
 * "git show --stat". Flags that change what a command does (--force, -delete,
 * ...) never create a rule and never match one.
 *
 * Commands that run other programs (sudo, xargs, bash -c) can't be saved at
 * all; commands whose rule must not broaden (interpreters, curl, rm) keep the
 * old two-token rule with prefix matching.
 *
 * Related headers:
 * - shell_parser.h: parses the command this module classifies
 * - auto_approve.h: uses shell_has_always_ask_flag() to gate read-only calls
 */

typedef struct {
    char **tokens;     /* ["grep"] or ["git","show"] or, for never-broaden, ["python","foo.py"] */
    int    count;
    int    exact;      /* 1 = legacy prefix semantics (never-broaden rules) */
} ShellRule;

typedef enum {
    SHELL_RULE_OK = 0,
    SHELL_RULE_UNSAFE_SYNTAX,     /* substitution, chain, unsplittable pipe, dangerous pattern */
    SHELL_RULE_REDIRECT,          /* writes to a file */
    SHELL_RULE_ALWAYS_ASK_FLAG,   /* e.g. --force */
    SHELL_RULE_WRAPPER,           /* sudo, env, xargs, nohup, timeout, nice, sh -c */
    SHELL_RULE_UNKNOWN_GLOBAL_OPT /* option before subcommand we can't classify */
} ShellRuleStatus;

/**
 * Derive the rule that "a" would save for a parsed command.
 *
 * @param p The parsed command (a single segment, already split)
 * @param out Output: the rule, filled only when SHELL_RULE_OK is returned.
 *            Caller must free with shell_rule_free().
 * @param out_detail Optional output: the offending token (e.g. "--force") for
 *                   SHELL_RULE_ALWAYS_ASK_FLAG and SHELL_RULE_UNKNOWN_GLOBAL_OPT,
 *                   NULL otherwise. Points into the parsed command's tokens.
 * @return SHELL_RULE_OK, or the reason no rule can be saved
 */
ShellRuleStatus shell_rule_from_parsed(const ParsedShellCommand *p, ShellRule *out,
                                       const char **out_detail);

/**
 * Check whether a command is covered by a saved rule.
 *
 * Exact rules match by token prefix; other rules match when the command derives
 * the same rule. A command that can't produce a rule never matches one.
 *
 * @param rule The saved rule
 * @param p The parsed command
 * @return 1 if the rule covers the command, 0 otherwise
 */
int shell_rule_matches(const ShellRule *rule, const ParsedShellCommand *p);

/**
 * Check whether a command carries a flag that must always be approved.
 *
 * @param p The parsed command
 * @param for_read_only 1 to consider only flags that also make an otherwise
 *                      read-only command unsafe (-delete, --output, sed -i);
 *                      0 to consider every always-ask flag
 * @param out_flag Optional output: the matching token, points into p->tokens
 * @return 1 if an always-ask flag is present, 0 otherwise
 */
int shell_has_always_ask_flag(const ParsedShellCommand *p, int for_read_only,
                              const char **out_flag);

/**
 * Short user-facing reason a command couldn't be saved as a rule.
 *
 * @param s The status from shell_rule_from_parsed()
 * @return Static string, "" for SHELL_RULE_OK
 */
const char *shell_rule_status_reason(ShellRuleStatus s);

/**
 * Render a rule for display, e.g. "git show …".
 *
 * @param rule The rule
 * @return Allocated string, or NULL on error. Caller must free.
 */
char *shell_rule_label(const ShellRule *rule);

/**
 * Free a rule's tokens and reset it. NULL safe.
 *
 * @param rule The rule to free
 */
void shell_rule_free(ShellRule *rule);

#endif /* SHELL_RULES_H */
