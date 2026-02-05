#ifndef TODO_TOOL_H
#define TODO_TOOL_H

#include "todo_manager.h"
#include "tools_system.h"
#include "../services/services.h"

int register_todo_tool(ToolRegistry* registry, TodoList* todo_list, Services* services);
int execute_todo_tool_call(const ToolCall *tool_call, ToolResult *result);
void clear_todo_tool_reference(void);

#endif