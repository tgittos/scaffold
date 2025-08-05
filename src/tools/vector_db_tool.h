#ifndef VECTOR_DB_TOOL_H
#define VECTOR_DB_TOOL_H

#include "tools_system.h"
#include "../db/vector_db.h"

int register_vector_db_tool(ToolRegistry *registry);

/**
 * Get the global vector database instance
 * Creates it if it doesn't exist
 * 
 * @return Global vector database instance
 */
vector_db_t* get_global_vector_db(void);

int execute_vector_db_create_index_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_delete_index_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_list_indices_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_add_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_update_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_delete_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_get_vector_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_search_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_add_text_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_add_chunked_text_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_vector_db_add_pdf_document_tool_call(const ToolCall *tool_call, ToolResult *result);

#endif