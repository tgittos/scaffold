#ifndef MEMORY_TOOL_H
#define MEMORY_TOOL_H

#include "tools_system.h"

typedef struct Services Services;

void memory_tool_set_services(Services* services);
int register_memory_tools(ToolRegistry *registry);

int execute_remember_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_forget_memory_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif