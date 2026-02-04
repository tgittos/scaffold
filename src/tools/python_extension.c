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

/**
 * Initialize Python interpreter and load tool files.
 */
static int python_extension_init(void) {
    if (python_interpreter_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize Python interpreter\n");
        return -1;
    }
    return 0;
}

/**
 * Register both the core Python tool and Python file tools.
 */
static int python_extension_register_tools(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    /* Register the core "python" execution tool */
    if (register_python_tool(registry) != 0) {
        fprintf(stderr, "Warning: Failed to register Python tool\n");
        return -1;
    }

    /* Register tools from ~/.local/ralph/tools/ */
    if (python_register_tool_schemas(registry) != 0) {
        fprintf(stderr, "Warning: Failed to register Python file tools\n");
        /* Non-fatal - core Python tool still works */
    }

    return 0;
}

/**
 * Shutdown Python interpreter.
 */
static void python_extension_shutdown(void) {
    python_interpreter_shutdown();
}

/**
 * The Python tool extension structure.
 */
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

/**
 * Register the Python extension with the tool extension system.
 * Call this early in main() before session_init().
 */
int python_extension_register(void) {
    return tool_extension_register(&g_python_extension);
}
