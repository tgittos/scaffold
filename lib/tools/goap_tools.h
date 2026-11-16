#ifndef GOAP_TOOLS_H
#define GOAP_TOOLS_H

#include "tools_system.h"

struct Services;
struct SubagentManager;

#include "../policy/approval_gate.h"

/** Most argv entries (with the NULL) goap_build_worker_args writes. */
#define GOAP_WORKER_ARGS_MAX 12

/**
 * Build a GOAP worker's argv. The worker gets the spawning process's approval
 * mode (--yolo / --auto / none) and --home; it is never forced to --yolo.
 *
 * @return Entries before the NULL, or -1 on bad arguments
 */
int goap_build_worker_args(char *args[], int capacity, char *exe_path, const char *queue_name,
                           const char *prompt_file, const ApprovalGateConfig *gate,
                           const char *home);

int register_goap_tools(ToolRegistry *registry);
void goap_tools_set_services(struct Services *services);
void goap_tools_set_subagent_manager(struct SubagentManager *mgr);

int execute_goap_dispatch(const ToolCall *tc, ToolResult *result);
int execute_goap_get_goal(const ToolCall *tc, ToolResult *result);
int execute_goap_list_actions(const ToolCall *tc, ToolResult *result);
int execute_goap_create_goal(const ToolCall *tc, ToolResult *result);
int execute_goap_create_actions(const ToolCall *tc, ToolResult *result);
int execute_goap_update_action(const ToolCall *tc, ToolResult *result);
int execute_goap_dispatch_action(const ToolCall *tc, ToolResult *result);
int execute_goap_update_world_state(const ToolCall *tc, ToolResult *result);
int execute_goap_check_complete(const ToolCall *tc, ToolResult *result);
int execute_goap_get_action_results(const ToolCall *tc, ToolResult *result);
int execute_goap_save_plan_document(const ToolCall *tc, ToolResult *result);

#endif /* GOAP_TOOLS_H */
