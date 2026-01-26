/**
 * Pattern Generator Implementation
 *
 * Generates allowlist patterns for the approval gate system's "allow always" feature.
 * Part of the approval gate policy module.
 */

#include "pattern_generator.h"
#include "approval_gate.h"
#include "shell_parser.h"
#include "tool_args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * Escape a string for use in a regex pattern.
 * Returns an allocated string that must be freed by the caller.
 */
static char *regex_escape(const char *str) {
    if (str == NULL) {
        return strdup("");
    }

    /* Characters that need escaping in POSIX extended regex */
    const char *meta = "\\^$.|?*+()[]{}";

    /* Calculate escaped length */
    size_t escaped_len = 0;
    for (const char *p = str; *p != '\0'; p++) {
        if (strchr(meta, *p) != NULL) {
            escaped_len += 2;  /* Backslash + char */
        } else {
            escaped_len += 1;
        }
    }

    char *result = malloc(escaped_len + 1);
    if (result == NULL) {
        return NULL;
    }

    char *out = result;
    for (const char *p = str; *p != '\0'; p++) {
        if (strchr(meta, *p) != NULL) {
            *out++ = '\\';
        }
        *out++ = *p;
    }
    *out = '\0';

    return result;
}

/**
 * Check if a path is in the root directory (no directory component other than ./).
 */
static int is_root_path(const char *path) {
    if (path == NULL) {
        return 0;
    }

    /* Skip leading ./ */
    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }

    /* If there's no remaining slash, it's a root file */
    return strchr(path, '/') == NULL;
}

/**
 * Check if a path is in /tmp (security-sensitive, always exact match).
 */
static int is_tmp_path(const char *path) {
    if (path == NULL) {
        return 0;
    }
    return strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/tmp", 4) == 0;
}

/**
 * Extract the directory component of a path.
 * Returns allocated string or NULL on failure.
 */
static char *get_directory(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    if (last_slash == NULL) {
        return strdup(".");
    }

    size_t dir_len = last_slash - path;
    if (dir_len == 0) {
        return strdup("/");
    }

    char *dir = malloc(dir_len + 1);
    if (dir == NULL) {
        return NULL;
    }

    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    return dir;
}

/**
 * Extract the file extension (including the dot).
 * Returns pointer to the extension within the input string, or NULL if none.
 */
static const char *get_extension(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    const char *basename = last_slash ? last_slash + 1 : path;
    const char *last_dot = strrchr(basename, '.');

    /* No extension or hidden file (starts with dot) */
    if (last_dot == NULL || last_dot == basename) {
        return NULL;
    }

    return last_dot;
}

/**
 * Extract the basename of a path.
 * Returns pointer within the input string.
 */
static const char *get_basename_simple(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

/* Signal handling and keypress reading moved to gate_prompter module */

/* =============================================================================
 * Public API
 * ========================================================================== */

void free_generated_pattern(GeneratedPattern *pattern) {
    if (pattern == NULL) {
        return;
    }

    free(pattern->pattern);
    pattern->pattern = NULL;

    if (pattern->command_prefix != NULL) {
        for (int i = 0; i < pattern->prefix_len; i++) {
            free(pattern->command_prefix[i]);
        }
        free(pattern->command_prefix);
        pattern->command_prefix = NULL;
    }
    pattern->prefix_len = 0;

    if (pattern->example_matches != NULL) {
        for (int i = 0; i < pattern->example_count; i++) {
            free(pattern->example_matches[i]);
        }
        free(pattern->example_matches);
        pattern->example_matches = NULL;
    }
    pattern->example_count = 0;
}

int generate_file_path_pattern(const char *path, GeneratedPattern *out_pattern) {
    if (path == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    /* Case 1: Root files get exact match */
    if (is_root_path(path)) {
        char *escaped = regex_escape(path);
        if (escaped == NULL) {
            return -1;
        }

        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);

        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Case 2: /tmp paths get exact match (security) */
    if (is_tmp_path(path)) {
        char *escaped = regex_escape(path);
        if (escaped == NULL) {
            return -1;
        }

        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);

        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Case 3: Regular paths - match directory and similar extensions */
    char *dir = get_directory(path);
    if (dir == NULL) {
        return -1;
    }

    char *escaped_dir = regex_escape(dir);
    free(dir);
    if (escaped_dir == NULL) {
        return -1;
    }

    const char *ext = get_extension(path);
    const char *basename = get_basename_simple(path);

    if (ext != NULL) {
        /* Has extension - match directory + any file with same extension */
        char *escaped_ext = regex_escape(ext);
        if (escaped_ext == NULL) {
            free(escaped_dir);
            return -1;
        }

        /* Check if basename has a prefix pattern (e.g., test_*.c) */
        const char *underscore = strchr(basename, '_');
        if (underscore != NULL && underscore < ext) {
            /* Has prefix pattern like "test_" - include it */
            size_t prefix_len = underscore - basename + 1;
            char *prefix = malloc(prefix_len + 1);
            if (prefix == NULL) {
                free(escaped_dir);
                free(escaped_ext);
                return -1;
            }
            memcpy(prefix, basename, prefix_len);
            prefix[prefix_len] = '\0';

            char *escaped_prefix = regex_escape(prefix);
            free(prefix);
            if (escaped_prefix == NULL) {
                free(escaped_dir);
                free(escaped_ext);
                return -1;
            }

            if (asprintf(&out_pattern->pattern, "^%s/%s.*%s$",
                         escaped_dir, escaped_prefix, escaped_ext) < 0) {
                free(escaped_dir);
                free(escaped_ext);
                free(escaped_prefix);
                return -1;
            }
            free(escaped_prefix);
        } else {
            /* No prefix pattern - match any file in directory with same extension */
            if (asprintf(&out_pattern->pattern, "^%s/.*%s$",
                         escaped_dir, escaped_ext) < 0) {
                free(escaped_dir);
                free(escaped_ext);
                return -1;
            }
        }
        free(escaped_ext);

        out_pattern->is_exact_match = 0;
        out_pattern->needs_confirmation = 1;

        /* Generate example matches (failures here are non-fatal) */
        out_pattern->example_matches = calloc(3, sizeof(char *));
        if (out_pattern->example_matches != NULL) {
            dir = get_directory(path);
            if (dir != NULL) {
                if (asprintf(&out_pattern->example_matches[0], "%s/foo%s", dir, ext) < 0) {
                    out_pattern->example_matches[0] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[1], "%s/bar%s", dir, ext) < 0) {
                    out_pattern->example_matches[1] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[2], "%s/other%s", dir, ext) < 0) {
                    out_pattern->example_matches[2] = NULL;
                }
                out_pattern->example_count = 3;
                free(dir);
            }
        }
    } else {
        /* No extension - exact match only */
        char *escaped = regex_escape(path);
        free(escaped_dir);
        if (escaped == NULL) {
            return -1;
        }

        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);

        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    free(escaped_dir);
    return 0;
}

int generate_shell_command_pattern(const char *command, GeneratedPattern *out_pattern) {
    if (command == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    /* Parse the shell command */
    ParsedShellCommand *parsed = parse_shell_command(command);
    if (parsed == NULL) {
        return -1;
    }

    /* Commands with chain operators, pipes, subshells, redirects, or dangerous patterns
     * cannot have patterns generated - they require exact approval each time */
    if (!shell_command_is_safe_for_matching(parsed)) {
        /* Return empty pattern - caller should handle this as "no pattern possible" */
        free_parsed_shell_command(parsed);
        out_pattern->is_exact_match = 1;  /* Can only approve this exact command */
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Generate command prefix: base command + first argument */
    int prefix_len = parsed->token_count >= 2 ? 2 : parsed->token_count;
    if (prefix_len <= 0) {
        free_parsed_shell_command(parsed);
        return -1;
    }

    out_pattern->command_prefix = calloc(prefix_len, sizeof(char *));
    if (out_pattern->command_prefix == NULL) {
        free_parsed_shell_command(parsed);
        return -1;
    }

    for (int i = 0; i < prefix_len; i++) {
        out_pattern->command_prefix[i] = strdup(parsed->tokens[i]);
        if (out_pattern->command_prefix[i] == NULL) {
            /* Cleanup on failure */
            for (int j = 0; j < i; j++) {
                free(out_pattern->command_prefix[j]);
            }
            free(out_pattern->command_prefix);
            out_pattern->command_prefix = NULL;
            free_parsed_shell_command(parsed);
            return -1;
        }
    }
    out_pattern->prefix_len = prefix_len;

    /* If command has more than 2 tokens, pattern is broader than exact match */
    if (parsed->token_count > prefix_len) {
        out_pattern->is_exact_match = 0;
        out_pattern->needs_confirmation = 1;

        /* Generate example matches (failures here are non-fatal) */
        out_pattern->example_matches = calloc(3, sizeof(char *));
        if (out_pattern->example_matches != NULL) {
            if (prefix_len == 1) {
                if (asprintf(&out_pattern->example_matches[0], "%s --help", parsed->tokens[0]) < 0) {
                    out_pattern->example_matches[0] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[1], "%s -v", parsed->tokens[0]) < 0) {
                    out_pattern->example_matches[1] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[2], "%s <any args>", parsed->tokens[0]) < 0) {
                    out_pattern->example_matches[2] = NULL;
                }
            } else {
                if (asprintf(&out_pattern->example_matches[0], "%s %s <any args>",
                             parsed->tokens[0], parsed->tokens[1]) < 0) {
                    out_pattern->example_matches[0] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[1], "%s %s -v",
                             parsed->tokens[0], parsed->tokens[1]) < 0) {
                    out_pattern->example_matches[1] = NULL;
                }
                if (asprintf(&out_pattern->example_matches[2], "%s %s --all",
                             parsed->tokens[0], parsed->tokens[1]) < 0) {
                    out_pattern->example_matches[2] = NULL;
                }
            }
            out_pattern->example_count = 3;
        }
    } else {
        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
    }

    free_parsed_shell_command(parsed);
    return 0;
}

int generate_network_url_pattern(const char *url, GeneratedPattern *out_pattern) {
    if (url == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    /* Parse URL: extract scheme and hostname */
    const char *scheme_end = strstr(url, "://");
    if (scheme_end == NULL) {
        /* Invalid URL format - return exact match */
        char *escaped = regex_escape(url);
        if (escaped == NULL) {
            return -1;
        }
        if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
            free(escaped);
            return -1;
        }
        free(escaped);
        out_pattern->is_exact_match = 1;
        out_pattern->needs_confirmation = 0;
        return 0;
    }

    /* Extract scheme */
    size_t scheme_len = scheme_end - url;
    char *scheme = malloc(scheme_len + 1);
    if (scheme == NULL) {
        return -1;
    }
    memcpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';

    /* Find hostname (ends at first / or : after ://) */
    const char *host_start = scheme_end + 3;
    const char *host_end = host_start;
    while (*host_end != '\0' && *host_end != '/' && *host_end != ':' && *host_end != '?') {
        host_end++;
    }

    size_t host_len = host_end - host_start;
    if (host_len == 0) {
        free(scheme);
        return -1;
    }

    char *hostname = malloc(host_len + 1);
    if (hostname == NULL) {
        free(scheme);
        return -1;
    }
    memcpy(hostname, host_start, host_len);
    hostname[host_len] = '\0';

    /* Escape scheme and hostname for regex */
    char *escaped_scheme = regex_escape(scheme);
    char *escaped_hostname = regex_escape(hostname);
    free(scheme);
    free(hostname);

    if (escaped_scheme == NULL || escaped_hostname == NULL) {
        free(escaped_scheme);
        free(escaped_hostname);
        return -1;
    }

    /* Generate pattern: ^scheme://hostname(/|$)
     * This requires either a path separator or end-of-string after hostname,
     * preventing subdomain spoofing like api.example.com.evil.com */
    if (asprintf(&out_pattern->pattern, "^%s://%s(/|$)",
                 escaped_scheme, escaped_hostname) < 0) {
        free(escaped_scheme);
        free(escaped_hostname);
        return -1;
    }

    free(escaped_scheme);
    free(escaped_hostname);

    out_pattern->is_exact_match = 0;
    out_pattern->needs_confirmation = 1;

    /* Generate example matches (failures here are non-fatal) */
    out_pattern->example_matches = calloc(3, sizeof(char *));
    if (out_pattern->example_matches != NULL) {
        /* Reconstruct base URL for examples */
        const char *base_end = host_end;
        size_t base_len = base_end - url;
        char *base_url = malloc(base_len + 1);
        if (base_url != NULL) {
            memcpy(base_url, url, base_len);
            base_url[base_len] = '\0';

            if (asprintf(&out_pattern->example_matches[0], "%s/any/path", base_url) < 0) {
                out_pattern->example_matches[0] = NULL;
            }
            if (asprintf(&out_pattern->example_matches[1], "%s/api/v1", base_url) < 0) {
                out_pattern->example_matches[1] = NULL;
            }
            if (asprintf(&out_pattern->example_matches[2], "%s", base_url) < 0) {
                out_pattern->example_matches[2] = NULL;
            }
            out_pattern->example_count = 3;
            free(base_url);
        }
    }

    return 0;
}

int generate_allowlist_pattern(const ToolCall *tool_call, GeneratedPattern *out_pattern) {
    if (tool_call == NULL || tool_call->name == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    GateCategory category = get_tool_category(tool_call->name);

    switch (category) {
        case GATE_CATEGORY_SHELL: {
            char *command = tool_args_get_command(tool_call);
            if (command == NULL) {
                return -1;
            }
            int result = generate_shell_command_pattern(command, out_pattern);
            free(command);
            return result;
        }

        case GATE_CATEGORY_NETWORK: {
            char *url = tool_args_get_url(tool_call);
            if (url == NULL) {
                return -1;
            }
            int result = generate_network_url_pattern(url, out_pattern);
            free(url);
            return result;
        }

        case GATE_CATEGORY_FILE_WRITE:
        case GATE_CATEGORY_FILE_READ: {
            char *path = tool_args_get_path(tool_call);
            if (path == NULL) {
                return -1;
            }
            int result = generate_file_path_pattern(path, out_pattern);
            free(path);
            return result;
        }

        default:
            /* For other categories, generate exact match on full arguments */
            if (tool_call->arguments != NULL) {
                char *escaped = regex_escape(tool_call->arguments);
                if (escaped == NULL) {
                    return -1;
                }
                if (asprintf(&out_pattern->pattern, "^%s$", escaped) < 0) {
                    free(escaped);
                    return -1;
                }
                free(escaped);
            }
            out_pattern->is_exact_match = 1;
            out_pattern->needs_confirmation = 0;
            return 0;
    }
}

/* confirm_pattern_scope moved to gate_prompter module */

int apply_generated_pattern(ApprovalGateConfig *config,
                            const char *tool_name,
                            const GeneratedPattern *pattern) {
    if (config == NULL || tool_name == NULL || pattern == NULL) {
        return -1;
    }

    /* A GeneratedPattern should have either command_prefix (for shell) OR
     * pattern (for other tools), but not both. If both are set, prefer
     * command_prefix since that's more specific for shell commands. */
    int has_prefix = (pattern->command_prefix != NULL && pattern->prefix_len > 0);
    int has_regex = (pattern->pattern != NULL);

    /* If neither is set, this is an invalid/empty pattern */
    if (!has_prefix && !has_regex) {
        return -1;
    }

    /* For shell commands, add to shell allowlist */
    if (has_prefix) {
        return approval_gate_add_shell_allowlist(
            config,
            (const char **)pattern->command_prefix,
            pattern->prefix_len,
            SHELL_TYPE_UNKNOWN);
    }

    /* For other tools, add regex pattern */
    if (has_regex) {
        return approval_gate_add_allowlist(config, tool_name, pattern->pattern);
    }

    return -1;
}
