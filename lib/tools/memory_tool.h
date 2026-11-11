#ifndef MEMORY_TOOL_H
#define MEMORY_TOOL_H

#include "tools_system.h"

typedef struct Services Services;

void memory_tool_set_services(Services* services);
int register_memory_tools(ToolRegistry *registry);

/* Index for the project containing the working directory at registration;
 * empty if it could not be resolved. */
const char* memory_tool_project_index(void);
/* Index shared by every project. */
const char* memory_tool_user_index(void);

int execute_remember_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_forget_memory_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif