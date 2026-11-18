#ifndef TODO_TOOL_H
#define TODO_TOOL_H

#include "todo_manager.h"
#include "tools_system.h"
#include "../services/services.h"

int register_todo_tool(ToolRegistry* registry, TodoList* todo_list, Services* services);
int execute_todo_tool_call(const ToolCall *tool_call, ToolResult *result);
void clear_todo_tool_reference(void);

/**
 * Return the per-project session key used to scope todos in the task store.
 * Derived from the nearest .git ancestor of the current working directory.
 * The returned pointer is into a static buffer; use the value immediately.
 */
const char *todo_session_key(void);

#endif