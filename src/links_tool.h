#ifndef LINKS_TOOL_H
#define LINKS_TOOL_H

#include "tools_system.h"

// Register the Links browser tool with the tool registry
int register_links_tool(ToolRegistry *registry);

// Execute a Links tool call to fetch web content
int execute_links_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif /* LINKS_TOOL_H */