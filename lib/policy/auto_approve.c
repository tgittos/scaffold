#include "auto_approve.h"
#include "protected_files.h"
#include "shell_parser.h"
#include "tool_args.h"
#include "../util/project_scope.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *READ_ONLY_COMMANDS[] = {
    "basename", "cat", "cd", "cmp", "cut", "df", "diff", "dirname", "du",
    "echo", "find", "git", "grep", "head", "ls", "pwd", "realpath", "stat",
    "tail", "tr", "true", "wc", "which", NULL
};

static const char *READ_ONLY_GIT_SUBCOMMANDS[] = {
    "blame", "diff", "grep", "log", "ls-files", "rev-parse", "show", "status", NULL
};

/* Flags that make an otherwise read-only command run programs or write files. */
static const char *WRITING_FLAG_PREFIXES[] = {
    "-exec", "-ok", "-delete", "-fprint", "-fls", "--output", NULL
};

static const char *PATCH_PATH_PREFIXES[] = {
    "*** Update File: ", "*** Add File: ", "*** Delete File: ", "*** Move to: ", NULL
};

static int in_list(const char *s, const char *const *list) {
    for (int i = 0; list[i] != NULL; i++) {
        if (strcmp(s, list[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int has_listed_prefix(const char *s, const char *const *prefixes) {
    for (int i = 0; prefixes[i] != NULL; i++) {
        if (strncmp(s, prefixes[i], strlen(prefixes[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

static int segment_is_read_only(const char *segment) {
    ParsedShellCommand *parsed = parse_shell_command_for_type(segment, SHELL_TYPE_POSIX);
    int ok = parsed != NULL && shell_command_is_safe_for_matching(parsed) &&
             parsed->token_count > 0 &&
             in_list(parsed->tokens[0], READ_ONLY_COMMANDS);

    if (ok && strcmp(parsed->tokens[0], "git") == 0) {
        ok = parsed->token_count >= 2 &&
             in_list(parsed->tokens[1], READ_ONLY_GIT_SUBCOMMANDS);
    }
    for (int i = 1; ok && i < parsed->token_count; i++) {
        ok = !has_listed_prefix(parsed->tokens[i], WRITING_FLAG_PREFIXES);
    }

    free_parsed_shell_command(parsed);
    return ok;
}

static int shell_is_read_only(const ToolCall *tool_call) {
    ShellType type = detect_shell_type();
    if (type != SHELL_TYPE_POSIX && type != SHELL_TYPE_UNKNOWN) {
        return 0;
    }

    char *command = tool_args_get_command(tool_call);
    if (command == NULL) {
        return 0;
    }

    char **segments = NULL;
    int count = shell_split_segments(command, SHELL_TYPE_POSIX, &segments);
    free(command);

    int ok = count > 0;
    for (int i = 0; i < count && ok; i++) {
        ok = segment_is_read_only(segments[i]);
    }

    free_shell_segments(segments, count);
    return ok;
}

static int has_dotdot_component(const char *path) {
    const char *p = path;
    while (*p != '\0') {
        while (*p == '/') {
            p++;
        }
        const char *end = strchr(p, '/');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            return 1;
        }
        p += len;
    }
    return 0;
}

/* Resolves path the way a write would reach it: symlinks in the existing
 * ancestors are followed, and ".." in the part that doesn't exist yet is
 * refused because realpath() can't see through it. */
static int resolve_write_target(const char *path, char out[PATH_MAX]) {
    char full[PATH_MAX];
    int n;
    if (path[0] == '/') {
        n = snprintf(full, sizeof(full), "%s", path);
    } else {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            return -1;
        }
        n = snprintf(full, sizeof(full), "%s/%s", cwd, path);
    }
    if (n < 0 || (size_t)n >= sizeof(full)) {
        return -1;
    }

    char probe[PATH_MAX];
    char resolved[PATH_MAX];
    memcpy(probe, full, (size_t)n + 1);
    size_t existing_len = (size_t)n;

    while (realpath(probe, resolved) == NULL) {
        char *slash = strrchr(probe, '/');
        if (slash == NULL) {
            return -1;
        }
        if (slash == probe) {
            probe[1] = '\0';
            existing_len = 1;
        } else {
            *slash = '\0';
            existing_len = (size_t)(slash - probe);
        }
    }

    const char *missing = full + existing_len;
    if (has_dotdot_component(missing)) {
        return -1;
    }

    const char *sep = (*missing != '\0' && *missing != '/' &&
                       resolved[strlen(resolved) - 1] != '/') ? "/" : "";
    n = snprintf(out, PATH_MAX, "%s%s%s", resolved, sep, missing);
    return (n < 0 || n >= PATH_MAX) ? -1 : 0;
}

static int write_stays_in_project(const char *path, const char *root) {
    if (path == NULL || *path == '\0' || is_protected_file(path)) {
        return 0;
    }

    char target[PATH_MAX];
    if (resolve_write_target(path, target) != 0 || is_protected_file(target)) {
        return 0;
    }

    size_t root_len = strlen(root);
    if (strncmp(target, root, root_len) != 0 || target[root_len] != '/') {
        return 0;
    }

    const char *rel = target + root_len;
    return !(strncmp(rel, "/.git", 5) == 0 && (rel[5] == '/' || rel[5] == '\0'));
}

static int patch_stays_in_project(const char *patch, const char *root) {
    int paths = 0;
    const char *line = patch;

    while (line != NULL && *line != '\0') {
        const char *end = strchr(line, '\n');
        size_t len = end != NULL ? (size_t)(end - line) : strlen(line);

        for (int i = 0; PATCH_PATH_PREFIXES[i] != NULL; i++) {
            size_t prefix_len = strlen(PATCH_PATH_PREFIXES[i]);
            if (len <= prefix_len || strncmp(line, PATCH_PATH_PREFIXES[i], prefix_len) != 0) {
                continue;
            }

            size_t path_len = len - prefix_len;
            while (path_len > 0 && (line[prefix_len + path_len - 1] == '\r' ||
                                    line[prefix_len + path_len - 1] == ' ')) {
                path_len--;
            }
            char *path = strndup(line + prefix_len, path_len);
            int ok = path != NULL && write_stays_in_project(path, root);
            free(path);
            if (!ok) {
                return 0;
            }
            paths++;
        }

        line = end != NULL ? end + 1 : NULL;
    }

    return paths > 0;
}

static int file_write_in_project(const ToolCall *tool_call) {
    char cwd[PATH_MAX];
    char root[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL ||
        project_scope_git_root(cwd, root, sizeof(root)) != 0) {
        return 0;
    }

    int ok = 0;
    if (strcmp(tool_call->name, "apply_patch") == 0) {
        char *patch = tool_args_get_string(tool_call, "patch");
        ok = patch != NULL && patch_stays_in_project(patch, root);
        free(patch);
    } else {
        char *path = tool_args_get_path(tool_call);
        ok = write_stays_in_project(path, root);
        free(path);
    }
    return ok;
}

int auto_approve_without_review(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->name == NULL) {
        return 0;
    }

    if (strcmp(tool_call->name, "shell") == 0) {
        return shell_is_read_only(tool_call);
    }
    if (strcmp(tool_call->name, "write_file") == 0 ||
        strcmp(tool_call->name, "apply_patch") == 0) {
        return file_write_in_project(tool_call);
    }
    return 0;
}
