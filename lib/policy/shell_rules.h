#ifndef SHELL_RULES_H
#define SHELL_RULES_H

#include "shell_parser.h"

/**
 * Shell allow rules created by pressing "a" at the approval prompt.
 *
 * A rule is the exact token list of the command segment it was saved from, and
 * it matches only a segment with the same tokens in the same order. Whitespace
 * and quoting don't matter, because the rule holds parsed tokens; anything else
 * does, so "a" on "git status" never covers "git status -sb".
 *
 * A segment can't be saved at all when the shell would turn it into something
 * else before it runs (substitution, chain, redirect, a glob, a brace
 * expansion) or when it carries a flag that must always be approved (--force,
 * -delete, curl -o, ...).
 *
 * Entries from the config file and --allow are not rules: they keep token
 * prefix matching, and shell_rule_is_broad() marks the ones --auto ignores.
 *
 * Related headers:
 * - shell_parser.h: parses the command this module classifies
 */

typedef struct {
    char **tokens;     /* Every token of the segment, e.g. ["make","test"] */
    int    count;
} ShellRule;

typedef enum {
    SHELL_RULE_OK = 0,
    SHELL_RULE_UNSAFE_SYNTAX,     /* substitution, chain, unsplittable pipe, dangerous pattern */
    SHELL_RULE_REDIRECT,          /* writes to a file */
    SHELL_RULE_ALWAYS_ASK_FLAG,   /* e.g. --force */
    SHELL_RULE_EXPANSION          /* a variable, glob or brace the shell expands */
} ShellRuleStatus;

/**
 * Derive the rule that "a" would save for a parsed command.
 *
 * @param p The parsed command (a single segment, already split)
 * @param out Output: the rule, filled only when SHELL_RULE_OK is returned.
 *            Caller must free with shell_rule_free().
 * @param out_detail Optional output: the offending token (e.g. "--force") for
 *                   SHELL_RULE_ALWAYS_ASK_FLAG and SHELL_RULE_EXPANSION, NULL
 *                   otherwise. Points into the parsed command's tokens.
 * @return SHELL_RULE_OK, or the reason no rule can be saved
 */
ShellRuleStatus shell_rule_from_parsed(const ParsedShellCommand *p, ShellRule *out,
                                       const char **out_detail);

/**
 * Check whether a command is covered by a saved rule.
 *
 * Only a command that could itself be saved, with the rule's exact tokens,
 * matches.
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
 * @param out_flag Optional output: the matching token, points into p->tokens
 * @return 1 if an always-ask flag is present, 0 otherwise
 */
int shell_has_always_ask_flag(const ParsedShellCommand *p, const char **out_flag);

/**
 * Check whether a token carries a brace expansion the shell would turn into
 * several words: a "{" with a "," or ".." before its "}". A quoted program such
 * as awk's "{print}" or sed's "1{p}" is just text.
 *
 * @param token The token to inspect
 * @return 1 if the token would expand, 0 otherwise
 */
int shell_token_has_brace_expansion(const char *token);

/**
 * Short user-facing reason a command couldn't be saved as a rule.
 *
 * @param s The status from shell_rule_from_parsed()
 * @return Static string, "" for SHELL_RULE_OK
 */
const char *shell_rule_status_reason(ShellRuleStatus s);

/**
 * Render a rule for display, e.g. "\"git show HEAD\"".
 *
 * The quotes are part of the label so that several rules read as a list:
 * "make test", "git status".
 *
 * @param rule The rule
 * @return Allocated string, or NULL on error. Caller must free.
 */
char *shell_rule_label(const ShellRule *rule);

/**
 * Check whether a config or --allow entry covers too much to stand in for a
 * model review.
 *
 * Broad entries name a command that runs whatever it is given: an interpreter
 * or wrapper, or a package manager's run/exec/test with no script named. In
 * --auto such an entry is skipped so the call reaches the review.
 *
 * @param tokens The entry's tokens
 * @param count Number of tokens
 * @return 1 if the entry is too broad, 0 otherwise
 */
int shell_rule_is_broad(const char *const *tokens, int count);

/**
 * Free a rule's tokens and reset it. NULL safe.
 *
 * @param rule The rule to free
 */
void shell_rule_free(ShellRule *rule);

#endif /* SHELL_RULES_H */
