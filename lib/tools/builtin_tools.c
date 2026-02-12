/**
 * builtin_tools.c - Built-in tool registration implementation
 *
 * Registers all built-in tools that are compiled into the binary.
 * External tools (e.g., Python) are registered via the tool extension system.
 */

#include "builtin_tools.h"
#include "vector_db_tool.h"
#include "memory_tool.h"
#include "mode_tool.h"
#include "pdf_tool.h"
#include "messaging_tool.h"
#include "goap_tools.h"
#include "../util/app_home.h"
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

    if (register_mode_tool(registry) != 0) {
        return -1;
    }

    /* Subagents communicate with the parent via the harness, not messaging tools */
    const char* is_subagent = getenv("AGENT_IS_SUBAGENT");
    if (is_subagent == NULL || strcmp(is_subagent, "1") != 0) {
        if (register_messaging_tools(registry) != 0) {
            fprintf(stderr, "Warning: Failed to register messaging tools\n");
        }
    }

    /* GOAP tools are only available in scaffold mode */
    if (strcmp(app_home_get_app_name(), "scaffold") == 0) {
        if (register_goap_tools(registry) != 0) {
            fprintf(stderr, "Warning: Failed to register GOAP tools\n");
        }
    }

    /* Note: Python tools are registered via tool_extension_init_all() called from
     * session_init() after all extensions have been registered. This allows lib/
     * to remain independent of Python-specific code. */

    tool_set_cacheable(registry, "pdf_extract_text", 1);

    tool_set_thread_safe(registry, "recall_memories", 1);
    tool_set_thread_safe(registry, "remember", 1);
    tool_set_thread_safe(registry, "forget_memory", 1);
    tool_set_thread_safe(registry, "pdf_extract_text", 1);

    return 0;
}
