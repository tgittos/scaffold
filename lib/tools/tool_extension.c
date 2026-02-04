/**
 * lib/tools/tool_extension.c - Tool Extension Interface Implementation
 *
 * Manages registration and lifecycle of external tool extensions.
 */

#include "tool_extension.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EXTENSIONS 8

static ToolExtension g_extensions[MAX_EXTENSIONS];
static int g_extension_count = 0;
static int g_initialized = 0;

int tool_extension_register(const ToolExtension *extension) {
    if (extension == NULL) {
        return -1;
    }

    if (g_extension_count >= MAX_EXTENSIONS) {
        fprintf(stderr, "Warning: Maximum tool extensions (%d) reached, cannot register '%s'\n",
                MAX_EXTENSIONS, extension->name ? extension->name : "unknown");
        return -1;
    }

    g_extensions[g_extension_count] = *extension;
    g_extension_count++;

    return 0;
}

void tool_extension_unregister_all(void) {
    g_extension_count = 0;
    g_initialized = 0;
}

int tool_extension_init_all(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    if (g_initialized) {
        return 0;
    }

    int any_failed = 0;

    for (int i = 0; i < g_extension_count; i++) {
        ToolExtension *ext = &g_extensions[i];

        if (ext->init != NULL) {
            if (ext->init() != 0) {
                fprintf(stderr, "Warning: Failed to initialize extension '%s'\n",
                        ext->name ? ext->name : "unknown");
                any_failed = 1;
                continue;
            }
        }

        if (ext->register_tools != NULL) {
            if (ext->register_tools(registry) != 0) {
                fprintf(stderr, "Warning: Failed to register tools for extension '%s'\n",
                        ext->name ? ext->name : "unknown");
                any_failed = 1;
            }
        }
    }

    g_initialized = 1;
    return any_failed ? -1 : 0;
}

void tool_extension_shutdown_all(void) {
    if (!g_initialized) {
        return;
    }

    for (int i = g_extension_count - 1; i >= 0; i--) {
        ToolExtension *ext = &g_extensions[i];
        if (ext->shutdown != NULL) {
            ext->shutdown();
        }
    }

    g_initialized = 0;
}

int tool_extension_is_extension_tool(const char *name) {
    if (name == NULL) {
        return 0;
    }

    for (int i = 0; i < g_extension_count; i++) {
        ToolExtension *ext = &g_extensions[i];
        if (ext->metadata.is_extension_tool != NULL &&
            ext->metadata.is_extension_tool(name)) {
            return 1;
        }
    }

    return 0;
}

const char* tool_extension_get_gate_category(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_extension_count; i++) {
        ToolExtension *ext = &g_extensions[i];

        if (ext->metadata.is_extension_tool != NULL &&
            ext->metadata.is_extension_tool(name)) {

            if (ext->metadata.get_gate_category != NULL) {
                return ext->metadata.get_gate_category(name);
            }
            return NULL;
        }
    }

    return NULL;
}

const char* tool_extension_get_match_arg(const char *name) {
    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < g_extension_count; i++) {
        ToolExtension *ext = &g_extensions[i];

        if (ext->metadata.is_extension_tool != NULL &&
            ext->metadata.is_extension_tool(name)) {

            if (ext->metadata.get_match_arg != NULL) {
                return ext->metadata.get_match_arg(name);
            }
            return NULL;
        }
    }

    return NULL;
}

char* tool_extension_get_tools_description(void) {
    size_t total_len = 0;
    char *descriptions[MAX_EXTENSIONS];
    int desc_count = 0;

    for (int i = 0; i < g_extension_count; i++) {
        ToolExtension *ext = &g_extensions[i];
        if (ext->metadata.get_tools_description != NULL) {
            char *desc = ext->metadata.get_tools_description();
            if (desc != NULL && strlen(desc) > 0) {
                descriptions[desc_count] = desc;
                total_len += strlen(desc);
                desc_count++;
            }
        }
    }

    if (desc_count == 0) {
        return NULL;
    }

    char *combined = malloc(total_len + 1);
    if (combined == NULL) {
        for (int i = 0; i < desc_count; i++) {
            free(descriptions[i]);
        }
        return NULL;
    }

    combined[0] = '\0';
    for (int i = 0; i < desc_count; i++) {
        strcat(combined, descriptions[i]);
        free(descriptions[i]);
    }

    return combined;
}
