#include "ralph_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

static char *g_ralph_home = NULL;
static int g_initialized = 0;

/**
 * Resolve a relative path to an absolute path.
 * Returns a newly allocated string, or NULL on error.
 */
static char* resolve_to_absolute(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    // Already absolute
    if (path[0] == '/') {
        return strdup(path);
    }

    // Relative path - resolve against current working directory
    char cwd[PATH_MAX];
    memset(cwd, 0, sizeof(cwd));  // Initialize for valgrind
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }

    // Handle ./ prefix
    const char *rel = path;
    if (rel[0] == '.' && rel[1] == '/') {
        rel += 2;
    }

    size_t len = strlen(cwd) + 1 + strlen(rel) + 1;
    char *absolute = malloc(len);
    if (absolute == NULL) {
        return NULL;
    }

    snprintf(absolute, len, "%s/%s", cwd, rel);
    return absolute;
}

/**
 * Create directory and all parent directories.
 */
static int mkdir_recursive(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        return -1;
    }

    // Create each directory component
    char *p = path_copy;
    if (*p == '/') p++; // Skip leading slash

    while (*p != '\0') {
        while (*p != '/' && *p != '\0') p++;
        char saved = *p;
        *p = '\0';

        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }

        *p = saved;
        if (*p == '/') p++;
    }

    // Create final directory
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }

    free(path_copy);
    return 0;
}

int ralph_home_init(const char *cli_override) {
    // Clean up any previous initialization
    if (g_ralph_home != NULL) {
        free(g_ralph_home);
        g_ralph_home = NULL;
    }
    g_initialized = 0;

    const char *source = NULL;
    char *resolved = NULL;

    // Priority 1: CLI override
    if (cli_override != NULL && cli_override[0] != '\0') {
        source = cli_override;
    }
    // Priority 2: Environment variable
    else {
        const char *env_home = getenv("RALPH_HOME");
        if (env_home != NULL && env_home[0] != '\0') {
            source = env_home;
        }
    }

    // Resolve the path
    if (source != NULL) {
        resolved = resolve_to_absolute(source);
        if (resolved == NULL) {
            return -1;
        }
        g_ralph_home = resolved;
    }
    // Priority 3: Default path
    else {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            return -1;
        }

        size_t len = strlen(home) + strlen("/.local/ralph") + 1;
        g_ralph_home = malloc(len);
        if (g_ralph_home == NULL) {
            return -1;
        }
        snprintf(g_ralph_home, len, "%s/.local/ralph", home);
    }

    g_initialized = 1;
    return 0;
}

const char* ralph_home_get(void) {
    return g_ralph_home;
}

char* ralph_home_path(const char *relative_path) {
    if (g_ralph_home == NULL || relative_path == NULL) {
        return NULL;
    }

    // Skip leading slash in relative path
    const char *rel = relative_path;
    if (rel[0] == '/') {
        rel++;
    }

    size_t len = strlen(g_ralph_home) + 1 + strlen(rel) + 1;
    char *full_path = malloc(len);
    if (full_path == NULL) {
        return NULL;
    }

    snprintf(full_path, len, "%s/%s", g_ralph_home, rel);
    return full_path;
}

int ralph_home_ensure_exists(void) {
    if (g_ralph_home == NULL) {
        return -1;
    }

    return mkdir_recursive(g_ralph_home);
}

void ralph_home_cleanup(void) {
    free(g_ralph_home);
    g_ralph_home = NULL;
    g_initialized = 0;
}

int ralph_home_is_initialized(void) {
    return g_initialized;
}
