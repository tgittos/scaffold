#include "pattern_generator.h"
#include "approval_gate.h"
#include "shell_parser.h"
#include "tool_args.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *regex_escape(const char *str) {
    if (str == NULL) {
        return strdup("");
    }

    const char *meta = "\\^$.|?*+()[]{}";
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

static int is_root_path(const char *path) {
    if (path == NULL) {
        return 0;
    }

    if (path[0] == '.' && path[1] == '/') {
        path += 2;
    }

    return strchr(path, '/') == NULL;
}

static int is_tmp_path(const char *path) {
    if (path == NULL) {
        return 0;
    }
    return strncmp(path, "/tmp/", 5) == 0 || strncmp(path, "/tmp", 4) == 0;
}

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

static const char *get_extension(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    const char *basename = last_slash ? last_slash + 1 : path;
    const char *last_dot = strrchr(basename, '.');

    if (last_dot == NULL || last_dot == basename) {
        return NULL;
    }

    return last_dot;
}

static const char *get_basename_simple(const char *path) {
    if (path == NULL) {
        return NULL;
    }

    const char *last_slash = strrchr(path, '/');
    return last_slash ? last_slash + 1 : path;
}

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

    /* /tmp paths always get exact match to prevent overly broad patterns */
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
        char *escaped_ext = regex_escape(ext);
        if (escaped_ext == NULL) {
            free(escaped_dir);
            return -1;
        }

        const char *underscore = strchr(basename, '_');
        if (underscore != NULL && underscore < ext) {
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

int generate_network_url_pattern(const char *url, GeneratedPattern *out_pattern) {
    if (url == NULL || out_pattern == NULL) {
        return -1;
    }

    memset(out_pattern, 0, sizeof(*out_pattern));

    const char *scheme_end = strstr(url, "://");
    if (scheme_end == NULL) {
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

    size_t scheme_len = scheme_end - url;
    char *scheme = malloc(scheme_len + 1);
    if (scheme == NULL) {
        return -1;
    }
    memcpy(scheme, url, scheme_len);
    scheme[scheme_len] = '\0';

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

    char *escaped_scheme = regex_escape(scheme);
    char *escaped_hostname = regex_escape(hostname);
    free(scheme);
    free(hostname);

    if (escaped_scheme == NULL || escaped_hostname == NULL) {
        free(escaped_scheme);
        free(escaped_hostname);
        return -1;
    }

    /* Require path separator or end-of-string after hostname to
     * prevent subdomain spoofing (e.g., api.example.com.evil.com) */
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

    out_pattern->example_matches = calloc(3, sizeof(char *));
    if (out_pattern->example_matches != NULL) {
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
        case GATE_CATEGORY_SHELL:
            /* Shell calls are saved as shell rules, not patterns. */
            return -1;

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

int apply_generated_pattern(ApprovalGateConfig *config,
                            const char *tool_name,
                            const GeneratedPattern *pattern) {
    if (config == NULL || tool_name == NULL || pattern == NULL) {
        return -1;
    }

    int has_prefix = (pattern->command_prefix != NULL && pattern->prefix_len > 0);
    int has_regex = (pattern->pattern != NULL);

    if (!has_prefix && !has_regex) {
        return -1;
    }

    if (has_prefix) {
        return approval_gate_add_shell_allowlist(
            config,
            (const char **)pattern->command_prefix,
            pattern->prefix_len,
            SHELL_TYPE_UNKNOWN);
    }

    if (has_regex) {
        return approval_gate_add_allowlist(config, tool_name, pattern->pattern);
    }

    return -1;
}
