#include "app_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>

static char *g_app_home = NULL;
static int g_initialized = 0;
static char *g_app_name = NULL;

static char* resolve_to_absolute(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    if (path[0] == '/') {
        return strdup(path);
    }

    char cwd[PATH_MAX];
    memset(cwd, 0, sizeof(cwd));
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return NULL;
    }

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

static int mkdir_recursive(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        return -1;
    }

    char *p = path_copy;
    if (*p == '/') p++;

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

    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        free(path_copy);
        return -1;
    }

    free(path_copy);
    return 0;
}

void app_home_set_app_name(const char *name) {
    free(g_app_name);
    g_app_name = (name != NULL) ? strdup(name) : NULL;
}

int app_home_init(const char *cli_override) {
    if (g_app_home != NULL) {
        free(g_app_home);
        g_app_home = NULL;
    }
    g_initialized = 0;

    const char *app_name = app_home_get_app_name();

    const char *source = NULL;
    char *resolved = NULL;

    if (cli_override != NULL && cli_override[0] != '\0') {
        source = cli_override;
    }
    else {
        /* Build env var name: <APP_NAME>_HOME (uppercased) */
        size_t name_len = strlen(app_name);
        char *env_var = malloc(name_len + strlen("_HOME") + 1);
        if (env_var == NULL) {
            return -1;
        }
        for (size_t i = 0; i < name_len; i++) {
            env_var[i] = (char)toupper((unsigned char)app_name[i]);
        }
        strcpy(env_var + name_len, "_HOME");

        const char *env_home = getenv(env_var);
        free(env_var);

        if (env_home != NULL && env_home[0] != '\0') {
            source = env_home;
        }
    }

    if (source != NULL) {
        resolved = resolve_to_absolute(source);
        if (resolved == NULL) {
            return -1;
        }
        g_app_home = resolved;
    }
    else {
        const char *home = getenv("HOME");
        if (home == NULL || home[0] == '\0') {
            return -1;
        }

        /* Build default path: ~/.local/<app_name> */
        size_t len = strlen(home) + strlen("/.local/") + strlen(app_name) + 1;
        g_app_home = malloc(len);
        if (g_app_home == NULL) {
            return -1;
        }
        snprintf(g_app_home, len, "%s/.local/%s", home, app_name);
    }

    g_initialized = 1;
    return 0;
}

const char* app_home_get(void) {
    return g_app_home;
}

char* app_home_path(const char *relative_path) {
    if (g_app_home == NULL || relative_path == NULL) {
        return NULL;
    }

    const char *rel = relative_path;
    if (rel[0] == '/') {
        rel++;
    }

    size_t len = strlen(g_app_home) + 1 + strlen(rel) + 1;
    char *full_path = malloc(len);
    if (full_path == NULL) {
        return NULL;
    }

    snprintf(full_path, len, "%s/%s", g_app_home, rel);
    return full_path;
}

int app_home_ensure_exists(void) {
    if (g_app_home == NULL) {
        return -1;
    }

    return mkdir_recursive(g_app_home);
}

void app_home_cleanup(void) {
    free(g_app_home);
    g_app_home = NULL;
    g_initialized = 0;
    free(g_app_name);
    g_app_name = NULL;
}

int app_home_is_initialized(void) {
    return g_initialized;
}

const char* app_home_get_app_name(void) {
    return g_app_name ? g_app_name : "ralph";
}
