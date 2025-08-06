#ifndef CONVERSATION_HISTORY_TOOL_H
#define CONVERSATION_HISTORY_TOOL_H

#include "tools_system.h"

int register_conversation_history_tool(ToolRegistry *registry);

int execute_get_conversation_history_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_search_conversation_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif // CONVERSATION_HISTORY_TOOL_H