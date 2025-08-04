#include "todo_tool.h"
#include "todo_display.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int todo_tool_init(TodoTool* tool) {
    if (!tool) return -1;
    
    tool->todo_list = malloc(sizeof(TodoList));
    if (!tool->todo_list) return -1;
    
    if (todo_list_init(tool->todo_list) != 0) {
        free(tool->todo_list);
        tool->todo_list = NULL;
        return -1;
    }
    
    return 0;
}

void todo_tool_destroy(TodoTool* tool) {
    if (!tool) return;
    
    if (tool->todo_list) {
        todo_list_destroy(tool->todo_list);
        free(tool->todo_list);
        tool->todo_list = NULL;
    }
}

char* todo_tool_create(TodoTool* tool, const char* content, const char* priority_str) {
    if (!tool || !tool->todo_list || !content) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    TodoPriority priority = TODO_PRIORITY_MEDIUM;
    if (priority_str) {
        priority = todo_priority_from_string(priority_str);
    }
    
    char id[TODO_MAX_ID_LENGTH] = {0};
    if (todo_create(tool->todo_list, content, priority, id) != 0) {
        return strdup("{\"error\":\"Failed to create todo\"}");
    }
    
    char* result = malloc(256);
    if (!result) return NULL;
    
    snprintf(result, 256, 
        "{\"success\":true,\"id\":\"%s\",\"content\":\"%s\",\"priority\":\"%s\"}", 
        id, content, todo_priority_to_string(priority));
    
    return result;
}

char* todo_tool_update_status(TodoTool* tool, const char* id, const char* status_str) {
    if (!tool || !tool->todo_list || !id || !status_str) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    TodoStatus status = todo_status_from_string(status_str);
    if (todo_update_status(tool->todo_list, id, status) != 0) {
        return strdup("{\"error\":\"Todo not found or update failed\"}");
    }
    
    char* result = malloc(128);
    if (!result) return NULL;
    
    snprintf(result, 128, 
        "{\"success\":true,\"id\":\"%s\",\"status\":\"%s\"}", 
        id, todo_status_to_string(status));
    
    return result;
}

char* todo_tool_update_priority(TodoTool* tool, const char* id, const char* priority_str) {
    if (!tool || !tool->todo_list || !id || !priority_str) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    TodoPriority priority = todo_priority_from_string(priority_str);
    if (todo_update_priority(tool->todo_list, id, priority) != 0) {
        return strdup("{\"error\":\"Todo not found or update failed\"}");
    }
    
    char* result = malloc(128);
    if (!result) return NULL;
    
    snprintf(result, 128, 
        "{\"success\":true,\"id\":\"%s\",\"priority\":\"%s\"}", 
        id, todo_priority_to_string(priority));
    
    return result;
}

char* todo_tool_delete(TodoTool* tool, const char* id) {
    if (!tool || !tool->todo_list || !id) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    if (todo_delete(tool->todo_list, id) != 0) {
        return strdup("{\"error\":\"Todo not found or delete failed\"}");
    }
    
    char* result = malloc(64);
    if (!result) return NULL;
    
    snprintf(result, 64, "{\"success\":true,\"deleted_id\":\"%s\"}", id);
    return result;
}

char* todo_tool_list(TodoTool* tool, const char* status_filter, const char* min_priority) {
    if (!tool || !tool->todo_list) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    int filter_status = -1;
    if (status_filter && strlen(status_filter) > 0) {
        filter_status = todo_status_from_string(status_filter);
    }
    
    TodoPriority min_pri = TODO_PRIORITY_LOW;
    if (min_priority && strlen(min_priority) > 0) {
        min_pri = todo_priority_from_string(min_priority);
    }
    
    Todo* filtered_todos = NULL;
    size_t count = 0;
    
    if (todo_list_filter(tool->todo_list, filter_status, min_pri, &filtered_todos, &count) != 0) {
        return strdup("{\"error\":\"Failed to filter todos\"}");
    }
    
    size_t buffer_size = 1024 + (count * 256);
    char* result = malloc(buffer_size);
    if (!result) {
        free(filtered_todos);
        return NULL;
    }
    
    strcpy(result, "{\"todos\":[");
    
    for (size_t i = 0; i < count; i++) {
        Todo* todo = &filtered_todos[i];
        char todo_json[512] = {0};
        
        snprintf(todo_json, sizeof(todo_json),
            "%s{\"id\":\"%s\",\"content\":\"%.256s\",\"status\":\"%s\",\"priority\":\"%s\"}",
            (i > 0) ? "," : "",
            todo->id,
            todo->content,
            todo_status_to_string(todo->status),
            todo_priority_to_string(todo->priority)
        );
        
        strcat(result, todo_json);
    }
    
    strcat(result, "]}");
    free(filtered_todos);
    return result;
}

char* todo_tool_serialize(TodoTool* tool) {
    if (!tool || !tool->todo_list) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    return todo_serialize_json(tool->todo_list);
}

char* todo_tool_execute(TodoTool* tool, const char* action, const char* args) {
    if (!tool || !action) {
        return strdup("{\"error\":\"Invalid parameters\"}");
    }
    
    (void)args;
    
    if (strcmp(action, "list") == 0) {
        return todo_tool_list(tool, NULL, NULL);
    }
    
    if (strcmp(action, "serialize") == 0) {
        return todo_tool_serialize(tool);
    }
    
    return strdup("{\"error\":\"Unknown action\"}");
}

// Global static reference to the todo list for tool calls
static TodoList* g_todo_list = NULL;

// Helper function to extract a JSON array parameter
static char* extract_json_array_parameter(const char *arguments, const char *param_name) {
    if (!arguments || !param_name) return NULL;
    
    char pattern[256] = {0};
    snprintf(pattern, sizeof(pattern), "\"%s\":", param_name);
    
    const char *start = strstr(arguments, pattern);
    if (!start) return NULL;
    
    start += strlen(pattern);
    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start == '[') {
        // Array value - find matching closing bracket
        const char *end = start + 1;
        int bracket_count = 1;
        
        while (*end && bracket_count > 0) {
            if (*end == '[') bracket_count++;
            else if (*end == ']') bracket_count--;
            else if (*end == '"') {
                // Skip quoted strings
                end++;
                while (*end && *end != '"') {
                    if (*end == '\\' && *(end + 1)) end += 2;
                    else end++;
                }
            }
            if (bracket_count > 0) end++;
        }
        
        if (bracket_count == 0) {
            size_t len = end - start;
            char *value = malloc(len + 1);
            if (value) {
                memcpy(value, start, len);
                value[len] = '\0';
            }
            return value;
        }
    }
    
    return NULL;
}

// Tool execution wrapper that interfaces with the tool system
int execute_todo_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }
    
    // Set up the result structure
    result->tool_call_id = strdup(tool_call->id ? tool_call->id : "unknown");
    if (!result->tool_call_id) return -1;
    
    // Check if global todo list is still valid
    if (g_todo_list == NULL) {
        result->result = strdup("{\"error\":\"Todo system not initialized\"}");
        result->success = 0;
        return result->result ? 0 : -1;
    }
    
    
    char* response = NULL;
    
    if (strcmp(tool_call->name, "TodoWrite") == 0) {
        if (tool_call->arguments) {
            // Extract the todos array from arguments
            char *todos_json = extract_json_array_parameter(tool_call->arguments, "todos");
            if (todos_json) {
                // Process the todos array - for now, we'll clear and rebuild the entire list
                // This is a simple implementation that replaces the entire todo list
                
                // Clear existing todos
                g_todo_list->count = 0;
                
                // Parse each todo item from the JSON array
                const char *current = todos_json + 1; // Skip opening '['
                while (*current && *current != ']') {
                    // Skip whitespace and commas
                    while (*current == ' ' || *current == '\t' || *current == ',' || *current == '\n') current++;
                    
                    if (*current == '{') {
                        // Find the end of this todo object
                        const char *obj_start = current;
                        const char *obj_end = current + 1;
                        int brace_count = 1;
                        
                        while (*obj_end && brace_count > 0) {
                            if (*obj_end == '{') brace_count++;
                            else if (*obj_end == '}') brace_count--;
                            else if (*obj_end == '"') {
                                obj_end++;
                                while (*obj_end && *obj_end != '"') {
                                    if (*obj_end == '\\' && *(obj_end + 1)) obj_end += 2;
                                    else obj_end++;
                                }
                            }
                            if (brace_count > 0) obj_end++;
                        }
                        
                        if (brace_count == 0) {
                            // Extract todo fields from this object
                            size_t obj_len = obj_end - obj_start;
                            char *todo_obj = malloc(obj_len + 1);
                            if (todo_obj) {
                                memcpy(todo_obj, obj_start, obj_len);
                                todo_obj[obj_len] = '\0';
                                
                                // Parse todo fields - look for "content", "status", "priority"
                                char content[TODO_MAX_CONTENT_LENGTH] = {0};
                                char status_str[32] = "pending";
                                char priority_str[32] = "medium";
                                char id[TODO_MAX_ID_LENGTH] = {0};
                                
                                // Extract content (try "content" first, then "title" as fallback)
                                const char *content_start = strstr(todo_obj, "\"content\":");
                                int skip_len = 10; // Length of "content":
                                if (!content_start) {
                                    // Fallback to "title" for AI compatibility
                                    content_start = strstr(todo_obj, "\"title\":");
                                    skip_len = 8; // Length of "title":
                                }
                                if (content_start) {
                                    content_start += skip_len;
                                    while (*content_start == ' ' || *content_start == '\t') content_start++;
                                    if (*content_start == '"') {
                                        content_start++;
                                        const char *content_end = content_start;
                                        while (*content_end && *content_end != '"') {
                                            if (*content_end == '\\' && *(content_end + 1)) content_end += 2;
                                            else content_end++;
                                        }
                                        size_t content_len = content_end - content_start;
                                        if (content_len < sizeof(content)) {
                                            memcpy(content, content_start, content_len);
                                            content[content_len] = '\0';
                                        }
                                    }
                                }
                                
                                // Extract status
                                const char *status_start = strstr(todo_obj, "\"status\":");
                                if (status_start) {
                                    status_start += 9; // Skip "status":
                                    while (*status_start == ' ' || *status_start == '\t') status_start++;
                                    if (*status_start == '"') {
                                        status_start++;
                                        const char *status_end = status_start;
                                        while (*status_end && *status_end != '"') {
                                            if (*status_end == '\\' && *(status_end + 1)) status_end += 2;
                                            else status_end++;
                                        }
                                        size_t status_len = status_end - status_start;
                                        if (status_len < sizeof(status_str)) {
                                            memcpy(status_str, status_start, status_len);
                                            status_str[status_len] = '\0';
                                        }
                                    }
                                }
                                
                                // Extract priority
                                const char *priority_start = strstr(todo_obj, "\"priority\":");
                                if (priority_start) {
                                    priority_start += 11; // Skip "priority":
                                    while (*priority_start == ' ' || *priority_start == '\t') priority_start++;
                                    if (*priority_start == '"') {
                                        priority_start++;
                                        const char *priority_end = priority_start;
                                        while (*priority_end && *priority_end != '"') {
                                            if (*priority_end == '\\' && *(priority_end + 1)) priority_end += 2;
                                            else priority_end++;
                                        }
                                        size_t priority_len = priority_end - priority_start;
                                        if (priority_len < sizeof(priority_str)) {
                                            memcpy(priority_str, priority_start, priority_len);
                                            priority_str[priority_len] = '\0';
                                        }
                                    }
                                }
                                
                                // Extract id (if provided)
                                const char *id_start = strstr(todo_obj, "\"id\":");
                                if (id_start) {
                                    id_start += 5; // Skip "id":
                                    while (*id_start == ' ' || *id_start == '\t') id_start++;
                                    if (*id_start == '"') {
                                        id_start++;
                                        const char *id_end = id_start;
                                        while (*id_end && *id_end != '"') {
                                            if (*id_end == '\\' && *(id_end + 1)) id_end += 2;
                                            else id_end++;
                                        }
                                        size_t id_len = id_end - id_start;
                                        if (id_len < sizeof(id)) {
                                            memcpy(id, id_start, id_len);
                                            id[id_len] = '\0';
                                        }
                                    }
                                }
                                
                                // Create the todo if we have content
                                if (strlen(content) > 0) {
                                    TodoPriority priority = todo_priority_from_string(priority_str);
                                    TodoStatus status = todo_status_from_string(status_str);
                                    
                                    char new_id[TODO_MAX_ID_LENGTH] = {0};
                                    if (todo_create(g_todo_list, content, priority, new_id) == 0) {
                                        // Update status if different from default
                                        if (status != TODO_STATUS_PENDING) {
                                            todo_update_status(g_todo_list, new_id, status);
                                        }
                                    }
                                }
                                
                                free(todo_obj);
                            }
                            current = obj_end;
                        } else {
                            break; // Malformed JSON
                        }
                    } else if (*current == ']') {
                        break; // End of array
                    } else {
                        current++; // Skip unexpected character
                    }
                }
                
                free(todos_json);
                
                // Update the todo display after successful modification
                todo_display_update(g_todo_list);
                
                // Return success message with updated todo list
                response = strdup("Todos updated. Continue with systematic execution of remaining tasks.");
            } else {
                response = strdup("{\"error\":\"No todos array found in arguments\"}");
            }
        } else {
            response = strdup("{\"error\":\"No arguments provided\"}");
        }
    } else {
        response = strdup("{\"error\":\"Unknown todo function\"}");
    }
    
    if (response == NULL) {
        result->result = strdup("{\"error\":\"Memory allocation failed\"}");
        result->success = 0;
    } else {
        result->result = response;
        result->success = 1;
    }
    
    return 0;
}

// Clear the global todo list reference (called during cleanup)
void clear_todo_tool_reference(void) {
    g_todo_list = NULL;
}

int register_todo_tool(ToolRegistry* registry, TodoList* todo_list) {
    if (registry == NULL || todo_list == NULL) {
        return -1;
    }
    
    // Store reference to todo list for tool calls
    g_todo_list = todo_list;
    
    // Expand registry if needed
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 1) * sizeof(ToolFunction));
    if (new_functions == NULL) {
        return -1;
    }
    
    registry->functions = new_functions;
    ToolFunction *todo_func = &registry->functions[registry->function_count];
    
    // Initialize todo tool function
    todo_func->name = strdup("TodoWrite");
    todo_func->description = strdup("Optional task breakdown tool. Use for complex multi-step work requiring systematic tracking. Not required for simple requests.");
    
    if (todo_func->name == NULL || todo_func->description == NULL) {
        free(todo_func->name);
        free(todo_func->description);
        return -1;
    }
    
    // Define parameters - simplified for now
    todo_func->parameter_count = 1;
    todo_func->parameters = malloc(1 * sizeof(ToolParameter));
    if (todo_func->parameters == NULL) {
        free(todo_func->name);
        free(todo_func->description);
        return -1;
    }
    
    // Parameter: todos array
    todo_func->parameters[0].name = strdup("todos");
    todo_func->parameters[0].type = strdup("array");
    todo_func->parameters[0].description = strdup("Array of todo items with id, content, status, and priority");
    todo_func->parameters[0].required = 1;
    todo_func->parameters[0].enum_values = NULL;
    todo_func->parameters[0].enum_count = 0;
    
    if (todo_func->parameters[0].name == NULL || 
        todo_func->parameters[0].type == NULL || 
        todo_func->parameters[0].description == NULL) {
        free(todo_func->name);
        free(todo_func->description);
        free(todo_func->parameters[0].name);
        free(todo_func->parameters[0].type);
        free(todo_func->parameters[0].description);
        free(todo_func->parameters);
        return -1;
    }
    
    // Note: Execution is handled by the main execute_tool_call function
    // which calls execute_todo_tool_call based on the tool name
    
    registry->function_count++;
    
    return 0;
}