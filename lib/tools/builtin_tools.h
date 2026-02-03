/**
 * builtin_tools.h - Built-in tool registration
 *
 * Registers all built-in tools that are compiled into the binary.
 */

#ifndef BUILTIN_TOOLS_H
#define BUILTIN_TOOLS_H

#include "tools_system.h"

/**
 * Register all built-in tools that are compiled into the binary
 *
 * @param registry Pointer to ToolRegistry structure to populate
 * @return 0 on success, -1 on failure
 */
int register_builtin_tools(ToolRegistry *registry);

#endif // BUILTIN_TOOLS_H
