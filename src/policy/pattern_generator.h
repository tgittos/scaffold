#ifndef PATTERN_GENERATOR_H
#define PATTERN_GENERATOR_H

/**
 * Pattern Generator Module
 *
 * Generates allowlist patterns for the approval gate system's "allow always" feature.
 * Supports generating patterns for file paths, shell commands, and network URLs.
 *
 * This module is part of the policy subsystem and is used by approval_gate.c
 */

#include "../tools/tools_system.h"  /* For ToolCall */

/* Forward declaration - ApprovalGateConfig is a typedef so we use struct tag */
typedef struct ApprovalGateConfig ApprovalGateConfig;

/**
 * Result of pattern generation that may need user confirmation.
 */
typedef struct {
    char *pattern;              /* Generated regex pattern (for non-shell tools) */
    char **command_prefix;      /* Generated command prefix (for shell tools) */
    int prefix_len;             /* Number of tokens in command prefix */
    int is_exact_match;         /* 1 if pattern is an exact match (no wildcards) */
    int needs_confirmation;     /* 1 if pattern matches more than current operation */
    char **example_matches;     /* Examples of what else the pattern would match */
    int example_count;          /* Number of example matches */
} GeneratedPattern;

/**
 * Result of pattern confirmation dialog.
 */
typedef enum {
    PATTERN_CONFIRMED,          /* User confirmed the broad pattern */
    PATTERN_EXACT_ONLY,         /* User wants exact match instead */
    PATTERN_EDITED,             /* User edited the pattern (stored in out_pattern) */
    PATTERN_CANCELLED           /* User cancelled the dialog */
} PatternConfirmResult;

/**
 * Generate an allowlist pattern for a file path operation.
 * Follows these rules:
 * - Root files (./README.md) get exact match
 * - /tmp paths get exact match (security)
 * - Other paths: match directory and similar extensions
 *
 * @param path The file path from the tool call
 * @param out_pattern Output: populated GeneratedPattern struct
 * @return 0 on success, -1 on failure
 */
int generate_file_path_pattern(const char *path, GeneratedPattern *out_pattern);

/**
 * Generate an allowlist entry for a shell command.
 * Extracts base command and first argument as prefix.
 * Commands with pipes, chains, or redirects return exact match only.
 *
 * @param command The shell command string
 * @param out_pattern Output: populated GeneratedPattern struct
 * @return 0 on success, -1 on failure
 */
int generate_shell_command_pattern(const char *command, GeneratedPattern *out_pattern);

/**
 * Generate an allowlist pattern for a network URL.
 * Extracts scheme + hostname, generates pattern requiring path separator
 * to prevent subdomain spoofing (e.g., api.example.com.evil.com).
 *
 * @param url The URL from the tool call
 * @param out_pattern Output: populated GeneratedPattern struct
 * @return 0 on success, -1 on failure
 */
int generate_network_url_pattern(const char *url, GeneratedPattern *out_pattern);

/**
 * Generate an allowlist pattern based on tool category.
 * Dispatches to the appropriate pattern generator based on category.
 *
 * @param tool_call The tool call to generate a pattern for
 * @param out_pattern Output: populated GeneratedPattern struct
 * @return 0 on success, -1 on failure
 */
int generate_allowlist_pattern(const ToolCall *tool_call, GeneratedPattern *out_pattern);

/* confirm_pattern_scope moved to gate_prompter module */

/**
 * Apply a generated pattern to the session allowlist.
 * For shell commands, adds to shell_allowlist.
 * For other tools, adds to regex allowlist.
 *
 * @param config Gate configuration to modify
 * @param tool_name Name of the tool
 * @param pattern The generated pattern to apply
 * @return 0 on success, -1 on failure
 */
int apply_generated_pattern(ApprovalGateConfig *config,
                            const char *tool_name,
                            const GeneratedPattern *pattern);

/**
 * Free resources held by a GeneratedPattern struct.
 *
 * @param pattern The pattern to clean up
 */
void free_generated_pattern(GeneratedPattern *pattern);

#endif /* PATTERN_GENERATOR_H */
