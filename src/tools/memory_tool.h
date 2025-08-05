#ifndef MEMORY_TOOL_H
#define MEMORY_TOOL_H

#include "tools_system.h"

int register_memory_tools(ToolRegistry *registry);

int execute_remember_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif