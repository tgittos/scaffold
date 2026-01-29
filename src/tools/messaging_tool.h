#ifndef MESSAGING_TOOL_H
#define MESSAGING_TOOL_H

#include "tools_system.h"

int register_messaging_tools(ToolRegistry *registry);

int execute_send_message_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_check_messages_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_subscribe_channel_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_publish_channel_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_check_channel_messages_tool_call(const ToolCall *tool_call, ToolResult *result);

void messaging_tool_set_agent_id(const char* agent_id);
const char* messaging_tool_get_agent_id(void);

#endif
