#ifndef TODO_TOOL_H
#define TODO_TOOL_H

#include "todo_manager.h"
#include "tools_system.h"

typedef struct {
    TodoList* todo_list;
} TodoTool;

int todo_tool_init(TodoTool* tool);
void todo_tool_destroy(TodoTool* tool);

char* todo_tool_execute(TodoTool* tool, const char* action, const char* args);

char* todo_tool_create(TodoTool* tool, const char* content, const char* priority_str);
char* todo_tool_update_status(TodoTool* tool, const char* id, const char* status_str);
char* todo_tool_update_priority(TodoTool* tool, const char* id, const char* priority_str);
char* todo_tool_delete(TodoTool* tool, const char* id);
char* todo_tool_list(TodoTool* tool, const char* status_filter, const char* min_priority);
char* todo_tool_serialize(TodoTool* tool);

// Tool system integration
int register_todo_tool(ToolRegistry* registry, TodoList* todo_list, const char* session_id);
int execute_todo_tool_call(const ToolCall *tool_call, ToolResult *result);
void clear_todo_tool_reference(void);

// Get the current session ID (for task_store integration)
const char* get_todo_session_id(void);

#endif