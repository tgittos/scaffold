/**
 * src/tools/python_extension.c - Python Tool Extension
 *
 * Implements the ToolExtension interface for Python tools.
 * This file bridges lib/'s tool extension system with src/'s Python
 * tool implementations.
 */

#include "lib/tools/tool_extension.h"
#include "python_tool.h"
#include "python_tool_files.h"
#include <stdio.h>

static int python_extension_init(void) {
    if (python_interpreter_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize Python interpreter\n");
        return -1;
    }
    return 0;
}

static int python_extension_register_tools(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    if (register_python_tool(registry) != 0) {
        fprintf(stderr, "Warning: Failed to register Python tool\n");
        return -1;
    }

    if (python_register_tool_schemas(registry) != 0) {
        fprintf(stderr, "Warning: Failed to register Python file tools\n");
        /* Non-fatal: core Python tool still works */
    }

    tool_set_cacheable(registry, "read_file", 1);
    tool_set_cacheable(registry, "list_dir", 1);
    tool_set_cacheable(registry, "file_info", 1);

    return 0;
}

static void python_extension_shutdown(void) {
    python_interpreter_shutdown();
}

static const ToolExtension g_python_extension = {
    .name = "python",
    .init = python_extension_init,
    .register_tools = python_extension_register_tools,
    .shutdown = python_extension_shutdown,
    .metadata = {
        .is_extension_tool = is_python_file_tool,
        .get_gate_category = python_tool_get_gate_category,
        .get_match_arg = python_tool_get_match_arg,
        .get_tools_description = python_get_loaded_tools_description
    }
};

int python_extension_register(void) {
    return tool_extension_register(&g_python_extension);
}
