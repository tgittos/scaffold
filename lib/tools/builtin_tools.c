/**
 * builtin_tools.c - Built-in tool registration implementation
 *
 * Registers all built-in tools that are compiled into the binary.
 */

#include "builtin_tools.h"
#include "vector_db_tool.h"
#include "memory_tool.h"
#include "pdf_tool.h"
#include "../../src/tools/python_tool.h"
#include "../../src/tools/python_tool_files.h"
#include "messaging_tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int register_builtin_tools(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    if (register_vector_db_tool(registry) != 0) {
        return -1;
    }

    if (register_memory_tools(registry) != 0) {
        return -1;
    }

    if (register_pdf_tool(registry) != 0) {
        return -1;
    }

    if (register_python_tool(registry) != 0) {
        return -1;
    }

    /* Initialize Python early so ~/.local/ralph/tools/ files can register their schemas */
    if (python_interpreter_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize Python interpreter\n");
    }

    if (python_register_tool_schemas(registry) != 0) {
        fprintf(stderr, "Warning: Failed to register Python file tools\n");
    }

    /* Subagents communicate with the parent via the harness, not messaging tools */
    const char* is_subagent = getenv("RALPH_IS_SUBAGENT");
    if (is_subagent == NULL || strcmp(is_subagent, "1") != 0) {
        if (register_messaging_tools(registry) != 0) {
            fprintf(stderr, "Warning: Failed to register messaging tools\n");
        }
    }

    return 0;
}
