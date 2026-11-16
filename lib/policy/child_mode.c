#include "child_mode.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const ApprovalGateConfig *g_process_gate = NULL;

void child_mode_set_process_gate(const ApprovalGateConfig *config) {
    g_process_gate = config;
}

const ApprovalGateConfig *child_mode_get_process_gate(void) {
    return g_process_gate;
}

const char *child_mode_mode_flag(const ApprovalGateConfig *config) {
    if (config == NULL) {
        return NULL;
    }
    if (!config->enabled) {
        return "--yolo";
    }
    return config->auto_mode ? "--auto" : NULL;
}

int child_mode_absolute_home(const char *home, char *buf, size_t size) {
    if (home == NULL || home[0] == '\0' || buf == NULL || size < 2) {
        return -1;
    }

    char resolved[PATH_MAX];
    if (realpath(home, resolved) != NULL) {
        if (strlen(resolved) >= size) {
            return -1;
        }
        snprintf(buf, size, "%s", resolved);
        return 0;
    }

    /* A home that doesn't exist yet still has to be absolute for the child. */
    if (home[0] == '/') {
        if (strlen(home) >= size) {
            return -1;
        }
        snprintf(buf, size, "%s", home);
        return 0;
    }

    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return -1;
    }
    if (strlen(cwd) + 1 + strlen(home) >= size) {
        return -1;
    }
    snprintf(buf, size, "%s/%s", cwd, home);
    return 0;
}

int child_mode_flags(const ApprovalGateConfig *config, const char *home,
                     const char *out[CHILD_MODE_FLAGS_MAX]) {
    if (out == NULL) {
        return 0;
    }
    int n = 0;
    const char *mode = child_mode_mode_flag(config);
    if (mode != NULL) {
        out[n++] = mode;
    }
    if (home != NULL && home[0] != '\0') {
        out[n++] = "--home";
        out[n++] = home;
    }
    return n;
}
