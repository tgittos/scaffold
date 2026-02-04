#ifndef ALLOWLIST_H
#define ALLOWLIST_H

/**
 * Allowlist Module
 *
 * Manages approval allowlists for the gate system.
 * Provides an opaque type that owns both regex-based entries (for file/network tools)
 * and command prefix entries (for shell commands).
 *
 * This module encapsulates regex compilation, pattern matching, and memory management.
 */

#include "shell_parser.h"  /* For ShellType */

/**
 * Opaque allowlist type.
 * Use allowlist_create() to create and allowlist_destroy() to free.
 */
typedef struct Allowlist Allowlist;

/**
 * Result of an allowlist match check.
 */
typedef enum {
    ALLOWLIST_NO_MATCH,     /* No matching entry found */
    ALLOWLIST_MATCHED       /* Entry matched */
} AllowlistMatchResult;

/**
 * Create a new allowlist.
 *
 * @return Newly allocated Allowlist, or NULL on failure
 */
Allowlist *allowlist_create(void);

/**
 * Destroy an allowlist and free all resources.
 *
 * @param al Allowlist to destroy (can be NULL)
 */
void allowlist_destroy(Allowlist *al);

/**
 * Add a regex-based allowlist entry.
 *
 * @param al Allowlist
 * @param tool Tool name to match exactly
 * @param pattern POSIX extended regex pattern
 * @return 0 on success, -1 on failure (invalid regex or allocation error)
 */
int allowlist_add_regex(Allowlist *al, const char *tool, const char *pattern);

/**
 * Add a shell command prefix entry.
 *
 * @param al Allowlist
 * @param prefix Array of command prefix tokens
 * @param prefix_len Number of tokens in prefix
 * @param shell_type Shell type to match, or SHELL_TYPE_UNKNOWN for any
 * @return 0 on success, -1 on failure
 */
int allowlist_add_shell(Allowlist *al, const char **prefix, int prefix_len,
                        ShellType shell_type);

/**
 * Check if a tool operation is allowed by a regex entry.
 *
 * @param al Allowlist
 * @param tool Tool name
 * @param target Target string to match against pattern (e.g., file path, URL)
 * @return ALLOWLIST_MATCHED if allowed, ALLOWLIST_NO_MATCH otherwise
 */
AllowlistMatchResult allowlist_check_regex(const Allowlist *al,
                                           const char *tool,
                                           const char *target);

/**
 * Check if a shell command is allowed.
 *
 * @param al Allowlist
 * @param tokens Parsed command tokens
 * @param token_count Number of tokens
 * @param shell_type Shell type of the command
 * @return ALLOWLIST_MATCHED if allowed, ALLOWLIST_NO_MATCH otherwise
 */
AllowlistMatchResult allowlist_check_shell(const Allowlist *al,
                                           const char **tokens,
                                           int token_count,
                                           ShellType shell_type);

/**
 * Get the count of regex entries.
 *
 * @param al Allowlist
 * @return Number of regex entries
 */
int allowlist_regex_count(const Allowlist *al);

/**
 * Get the count of shell entries.
 *
 * @param al Allowlist
 * @return Number of shell entries
 */
int allowlist_shell_count(const Allowlist *al);

/**
 * Clear entries added after a certain point (for inheritance).
 * Removes all entries added after the first `keep_regex` regex entries
 * and `keep_shell` shell entries.
 *
 * @param al Allowlist
 * @param keep_regex Number of regex entries to keep
 * @param keep_shell Number of shell entries to keep
 */
void allowlist_clear_session(Allowlist *al, int keep_regex, int keep_shell);

#endif /* ALLOWLIST_H */
