#include "todo_tool.h"
#include "todo_display.h"
#include "../db/task_store.h"
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

// Fixed global session ID - tasks persist across all ralph invocations
static const char* GLOBAL_SESSION_ID = "global";

// Sync in-memory TodoList from task_store
// This keeps TodoList as a cache of the SQLite-backed store
static void sync_todolist_from_store(void) {
    if (g_todo_list == NULL) {
        return;
    }

    task_store_t* store = task_store_get_instance();
    if (store == NULL) {
        return;  // SQLite unavailable, keep in-memory only
    }

    // Get all tasks (using global session)
    size_t count = 0;
    Task** tasks = task_store_list_by_session(store, GLOBAL_SESSION_ID, -1, &count);
    if (tasks == NULL) {
        return;
    }

    // Clear in-memory list
    g_todo_list->count = 0;

    // Copy tasks from store to in-memory list
    for (size_t i = 0; i < count; i++) {
        Task* task = tasks[i];
        if (task == NULL || task->content == NULL) continue;

        // Ensure list has capacity
        if (g_todo_list->count >= g_todo_list->capacity) {
            size_t new_capacity = g_todo_list->capacity * 2;
            if (new_capacity > TODO_MAX_COUNT) new_capacity = TODO_MAX_COUNT;
            if (new_capacity <= g_todo_list->capacity) break;  // At max

            Todo* new_todos = realloc(g_todo_list->todos, sizeof(Todo) * new_capacity);
            if (!new_todos) break;
            g_todo_list->todos = new_todos;
            g_todo_list->capacity = new_capacity;
        }

        // Copy task to todo
        Todo* todo = &g_todo_list->todos[g_todo_list->count];
        strncpy(todo->id, task->id, TODO_MAX_ID_LENGTH - 1);
        todo->id[TODO_MAX_ID_LENGTH - 1] = '\0';

        strncpy(todo->content, task->content, TODO_MAX_CONTENT_LENGTH - 1);
        todo->content[TODO_MAX_CONTENT_LENGTH - 1] = '\0';

        todo->status = (TodoStatus)task->status;
        todo->priority = (TodoPriority)task->priority;
        todo->created_at = task->created_at;
        todo->updated_at = task->updated_at;

        g_todo_list->count++;
    }

    task_free_list(tasks, count);
}

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
    bool operation_success = false;

    if (strcmp(tool_call->name, "TodoRead") == 0) {
        // Sync from SQLite to get latest state
        sync_todolist_from_store();

        // Build JSON response with all tasks
        size_t buffer_size = 256 + (g_todo_list->count * 512);
        response = malloc(buffer_size);
        if (response) {
            strcpy(response, "{\"tasks\":[");
            for (size_t i = 0; i < g_todo_list->count; i++) {
                Todo* todo = &g_todo_list->todos[i];
                char task_json[512] = {0};
                snprintf(task_json, sizeof(task_json),
                    "%s{\"id\":\"%s\",\"content\":\"%.256s\",\"status\":\"%s\",\"priority\":\"%s\"}",
                    (i > 0) ? "," : "",
                    todo->id,
                    todo->content,
                    todo_status_to_string(todo->status),
                    todo_priority_to_string(todo->priority)
                );
                strcat(response, task_json);
            }
            strcat(response, "]}");
            operation_success = true;
        }
    } else if (strcmp(tool_call->name, "TodoWrite") == 0) {
        if (tool_call->arguments) {
            // Extract the todos array from arguments
            char *todos_json = extract_json_array_parameter(tool_call->arguments, "todos");
            if (todos_json) {
                // Get task_store instance for persistence
                task_store_t* store = task_store_get_instance();
                int use_sqlite = (store != NULL);

                // Collect tasks for bulk replacement if using SQLite
                Task* bulk_tasks = NULL;
                size_t bulk_count = 0;
                size_t bulk_capacity = 0;

                if (use_sqlite) {
                    bulk_capacity = 16;  // Initial capacity
                    bulk_tasks = malloc(sizeof(Task) * bulk_capacity);
                    if (!bulk_tasks) use_sqlite = 0;  // Fallback to in-memory
                }

                // Clear in-memory list (will be repopulated from SQLite or directly)
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
                                char content[4096] = {0};  // Large buffer for content
                                char status_str[32] = "pending";
                                char priority_str[32] = "medium";
                                char parent_id[40] = {0};

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

                                // Extract parent_id (for subtask support)
                                const char *parent_start = strstr(todo_obj, "\"parent_id\":");
                                if (parent_start) {
                                    parent_start += 12; // Skip "parent_id":
                                    while (*parent_start == ' ' || *parent_start == '\t') parent_start++;
                                    if (*parent_start == '"') {
                                        parent_start++;
                                        const char *parent_end = parent_start;
                                        while (*parent_end && *parent_end != '"') {
                                            if (*parent_end == '\\' && *(parent_end + 1)) parent_end += 2;
                                            else parent_end++;
                                        }
                                        size_t parent_len = parent_end - parent_start;
                                        if (parent_len < sizeof(parent_id)) {
                                            memcpy(parent_id, parent_start, parent_len);
                                            parent_id[parent_len] = '\0';
                                        }
                                    }
                                }

                                // Create the task if we have content
                                if (strlen(content) > 0) {
                                    TaskPriority priority = task_priority_from_string(priority_str);
                                    TaskStatus status = task_status_from_string(status_str);

                                    if (use_sqlite && bulk_tasks) {
                                        // Grow array if needed
                                        if (bulk_count >= bulk_capacity) {
                                            bulk_capacity *= 2;
                                            Task* new_tasks = realloc(bulk_tasks, sizeof(Task) * bulk_capacity);
                                            if (new_tasks) {
                                                bulk_tasks = new_tasks;
                                            } else {
                                                // Continue with current capacity
                                            }
                                        }

                                        if (bulk_count < bulk_capacity) {
                                            Task* task = &bulk_tasks[bulk_count];
                                            memset(task, 0, sizeof(Task));
                                            task->content = strdup(content);
                                            task->status = status;
                                            task->priority = priority;
                                            // Copy parent_id safely (memset already zeroed the struct)
                                            size_t parent_len = strlen(parent_id);
                                            if (parent_len > 0 && parent_len < sizeof(task->parent_id)) {
                                                memcpy(task->parent_id, parent_id, parent_len);
                                            }
                                            task->created_at = time(NULL);
                                            task->updated_at = task->created_at;
                                            bulk_count++;
                                        }
                                    } else {
                                        // Fallback: in-memory only
                                        char new_id[TODO_MAX_ID_LENGTH] = {0};
                                        if (todo_create(g_todo_list, content, (TodoPriority)priority, new_id) == 0) {
                                            if (status != TASK_STATUS_PENDING) {
                                                todo_update_status(g_todo_list, new_id, (TodoStatus)status);
                                            }
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

                // Persist to SQLite if available
                if (use_sqlite && bulk_tasks && bulk_count > 0) {
                    task_store_replace_session_tasks(store, GLOBAL_SESSION_ID, bulk_tasks, bulk_count);
                    // Sync back to in-memory list for display
                    sync_todolist_from_store();
                }

                // Clean up bulk tasks
                if (bulk_tasks) {
                    for (size_t i = 0; i < bulk_count; i++) {
                        free(bulk_tasks[i].content);
                    }
                    free(bulk_tasks);
                }

                // Update the todo display after successful modification
                todo_display_update(g_todo_list);

                // Return success message - neutral, does not instruct further action
                response = strdup("Task list updated successfully.");
                operation_success = true;
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
        result->success = operation_success ? 1 : 0;
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

    // Load any existing tasks from SQLite
    // This enables persistence across ralph restarts
    sync_todolist_from_store();

    // Define parameters
    ToolParameter parameters[1];
    memset(parameters, 0, sizeof(parameters));

    // Define the schema for todo items in the array
    static const char* todo_items_schema =
        "{\"type\": \"object\", "
        "\"properties\": {"
            "\"id\": {\"type\": \"string\", \"description\": \"Unique identifier for the todo item\"},"
            "\"content\": {\"type\": \"string\", \"description\": \"The task description\"},"
            "\"status\": {\"type\": \"string\", \"enum\": [\"pending\", \"in_progress\", \"completed\"], \"description\": \"Current status of the task\"},"
            "\"priority\": {\"type\": \"string\", \"enum\": [\"low\", \"medium\", \"high\"], \"description\": \"Priority level\"}"
        "}, "
        "\"required\": [\"content\"]}";

    // Parameter 1: todos (required)
    parameters[0].name = strdup("todos");
    parameters[0].type = strdup("array");
    parameters[0].description = strdup("Array of todo items. Each item must have 'content' (task description). Optional: 'id', 'status' (pending/in_progress/completed), 'priority' (low/medium/high)");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;
    parameters[0].items_schema = strdup(todo_items_schema);

    // Check for allocation failures
    if (parameters[0].name == NULL ||
        parameters[0].type == NULL ||
        parameters[0].description == NULL ||
        parameters[0].items_schema == NULL) {
        // Cleanup on failure
        free(parameters[0].name);
        free(parameters[0].type);
        free(parameters[0].description);
        free(parameters[0].items_schema);
        return -1;
    }
    
    // Register TodoWrite tool
    int result = register_tool(registry, "TodoWrite",
                              "Write/replace the task list. Use for complex multi-step work requiring systematic tracking. Pass the complete list of tasks.",
                              parameters, 1, execute_todo_tool_call);

    // Clean up temporary parameter storage
    free(parameters[0].name);
    free(parameters[0].type);
    free(parameters[0].description);
    free(parameters[0].items_schema);

    if (result != 0) {
        return result;
    }

    // Register TodoRead tool (no parameters)
    result = register_tool(registry, "TodoRead",
                          "Read the current task list. Use this to check what tasks exist before modifying them.",
                          NULL, 0, execute_todo_tool_call);

    return result;
}