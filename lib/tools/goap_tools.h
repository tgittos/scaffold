#ifndef GOAP_TOOLS_H
#define GOAP_TOOLS_H

#include "tools_system.h"

struct Services;
struct SubagentManager;

int register_goap_tools(ToolRegistry *registry);
void goap_tools_set_services(struct Services *services);
void goap_tools_set_subagent_manager(struct SubagentManager *mgr);

int execute_goap_get_goal(const ToolCall *tc, ToolResult *result);
int execute_goap_list_actions(const ToolCall *tc, ToolResult *result);
int execute_goap_create_goal(const ToolCall *tc, ToolResult *result);
int execute_goap_create_actions(const ToolCall *tc, ToolResult *result);
int execute_goap_update_action(const ToolCall *tc, ToolResult *result);
int execute_goap_dispatch_action(const ToolCall *tc, ToolResult *result);
int execute_goap_update_world_state(const ToolCall *tc, ToolResult *result);
int execute_goap_check_complete(const ToolCall *tc, ToolResult *result);
int execute_goap_get_action_results(const ToolCall *tc, ToolResult *result);

#endif /* GOAP_TOOLS_H */
