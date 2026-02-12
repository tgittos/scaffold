#ifndef ORCHESTRATOR_TOOL_H
#define ORCHESTRATOR_TOOL_H

#include "tools_system.h"

struct Services;

int register_orchestrator_tools(ToolRegistry *registry);
void orchestrator_tool_set_services(struct Services *services);

int execute_execute_plan(const ToolCall *tc, ToolResult *result);
int execute_list_goals(const ToolCall *tc, ToolResult *result);
int execute_goal_status(const ToolCall *tc, ToolResult *result);
int execute_start_goal(const ToolCall *tc, ToolResult *result);
int execute_pause_goal(const ToolCall *tc, ToolResult *result);
int execute_cancel_goal(const ToolCall *tc, ToolResult *result);

#endif /* ORCHESTRATOR_TOOL_H */
