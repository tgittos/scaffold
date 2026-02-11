#ifndef MODE_TOOL_H
#define MODE_TOOL_H

#include "tools_system.h"

typedef struct AgentSession AgentSession;

/**
 * Wire the mode tool to the active session.
 * Must be called after session_init() so the tool can access current_mode.
 */
void mode_tool_set_session(AgentSession* session);

/**
 * Execute the switch_mode tool call.
 * Exposed for testing; production code goes through the tool registry.
 */
int execute_switch_mode_tool_call(const ToolCall* tool_call, ToolResult* result);

/**
 * Register the switch_mode tool into the tool registry.
 * @return 0 on success, -1 on failure
 */
int register_mode_tool(ToolRegistry* registry);

#endif /* MODE_TOOL_H */
