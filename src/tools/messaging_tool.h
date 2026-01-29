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
const char* messaging_tool_get_agent_id(void);
char* messaging_tool_get_agent_id_copy(void);  /* Thread-safe, caller must free */

void messaging_tool_set_parent_agent_id(const char* parent_id);
const char* messaging_tool_get_parent_agent_id(void);
char* messaging_tool_get_parent_agent_id_copy(void);  /* Thread-safe, caller must free */

void messaging_tool_cleanup(void);

#endif
