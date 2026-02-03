#ifndef MESSAGING_TOOL_H
#define MESSAGING_TOOL_H

#include "tools_system.h"

#define RALPH_PARENT_AGENT_ID_ENV "RALPH_PARENT_AGENT_ID"

int register_messaging_tools(ToolRegistry *registry);

int execute_send_message_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_check_messages_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_subscribe_channel_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_publish_channel_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_check_channel_messages_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_get_agent_info_tool_call(const ToolCall *tool_call, ToolResult *result);

void messaging_tool_set_agent_id(const char* agent_id);

/**
 * Get the current agent ID (thread-safe).
 * @return Allocated copy of agent ID. Caller must free. Returns NULL if not set.
 */
char* messaging_tool_get_agent_id(void);

void messaging_tool_set_parent_agent_id(const char* parent_id);

/**
 * Get the parent agent ID (thread-safe).
 * @return Allocated copy of parent ID. Caller must free. Returns NULL if not set.
 */
char* messaging_tool_get_parent_agent_id(void);

void messaging_tool_cleanup(void);

#endif
