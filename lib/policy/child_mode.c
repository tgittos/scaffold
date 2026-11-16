#include "child_mode.h"

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
