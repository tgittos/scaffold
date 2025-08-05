#ifndef VECTOR_DB_TOOL_H
#define VECTOR_DB_TOOL_H

#include "tools_system.h"

int register_vector_db_tool(ToolRegistry *registry);

int execute_vector_db_create_index_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_delete_index_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_list_indices_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_add_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_update_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_delete_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_get_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_search_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif