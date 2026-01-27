#include "todo_tool.h"
#include "todo_display.h"
#include "../db/task_store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static TodoList* g_todo_list = NULL;

// Tasks persist across all ralph invocations using a fixed session key
static const char* GLOBAL_SESSION_ID = "global";

// Keep in-memory TodoList in sync with the SQLite-backed store
static void sync_todolist_from_store(void) {
    if (g_todo_list == NULL) {
        return;
    }

    task_store_t* store = task_store_get_instance();
    if (store == NULL) {
        return;  // SQLite unavailable, keep in-memory only
    }

    size_t count = 0;
    Task** tasks = task_store_list_by_session(store, GLOBAL_SESSION_ID, -1, &count);
    if (tasks == NULL) {
        return;
    }

    TodoList_clear(g_todo_list);

    for (size_t i = 0; i < count; i++) {
        Task* task = tasks[i];
        if (task == NULL || task->content == NULL) continue;

        if (g_todo_list->count >= TODO_MAX_COUNT) break;

        Todo new_todo;
        strncpy(new_todo.id, task->id, TODO_MAX_ID_LENGTH - 1);
        new_todo.id[TODO_MAX_ID_LENGTH - 1] = '\0';

        strncpy(new_todo.content, task->content, TODO_MAX_CONTENT_LENGTH - 1);
        new_todo.content[TODO_MAX_CONTENT_LENGTH - 1] = '\0';

        new_todo.status = (TodoStatus)task->status;
        new_todo.priority = (TodoPriority)task->priority;
        new_todo.created_at = task->created_at;
        new_todo.updated_at = task->updated_at;

        if (TodoList_push(g_todo_list, new_todo) != 0) break;
    }

    task_free_list(tasks, count);
}

// Extract a JSON array value by key name using manual parsing
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

int execute_todo_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }
    
    result->tool_call_id = strdup(tool_call->id ? tool_call->id : "unknown");
    if (!result->tool_call_id) return -1;

    if (g_todo_list == NULL) {
        result->result = strdup("{\"error\":\"Todo system not initialized\"}");
        result->success = 0;
        return result->result ? 0 : -1;
    }
    
    
    char* response = NULL;
    bool operation_success = false;

    if (strcmp(tool_call->name, "TodoRead") == 0) {
        sync_todolist_from_store();

        size_t buffer_size = 256 + (g_todo_list->count * 512);
        response = malloc(buffer_size);
        if (response) {
            strcpy(response, "{\"tasks\":[");
            for (size_t i = 0; i < g_todo_list->count; i++) {
                Todo* todo = &g_todo_list->data[i];
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
            char *todos_json = extract_json_array_parameter(tool_call->arguments, "todos");
            if (todos_json) {
                task_store_t* store = task_store_get_instance();
                int use_sqlite = (store != NULL);

                Task* bulk_tasks = NULL;
                size_t bulk_count = 0;
                size_t bulk_capacity = 0;

                if (use_sqlite) {
                    bulk_capacity = 16;  // Initial capacity
                    bulk_tasks = malloc(sizeof(Task) * bulk_capacity);
                    if (!bulk_tasks) use_sqlite = 0;  // Fallback to in-memory
                }

                TodoList_clear(g_todo_list);

                const char *current = todos_json + 1; // Skip opening '['
                while (*current && *current != ']') {
                    // Skip whitespace and commas
                    while (*current == ' ' || *current == '\t' || *current == ',' || *current == '\n') current++;

                    if (*current == '{') {
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
                            size_t obj_len = obj_end - obj_start;
                            char *todo_obj = malloc(obj_len + 1);
                            if (todo_obj) {
                                memcpy(todo_obj, obj_start, obj_len);
                                todo_obj[obj_len] = '\0';

                                char content[4096] = {0};
                                char status_str[32] = "pending";
                                char priority_str[32] = "medium";
                                char parent_id[40] = {0};

                                // Try "content" first, then "title" for AI compatibility
                                const char *content_start = strstr(todo_obj, "\"content\":");
                                int skip_len = 10;
                                if (!content_start) {
                                    content_start = strstr(todo_obj, "\"title\":");
                                    skip_len = 8;
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

                                const char *status_start = strstr(todo_obj, "\"status\":");
                                if (status_start) {
                                    status_start += 9;
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

                                const char *priority_start = strstr(todo_obj, "\"priority\":");
                                if (priority_start) {
                                    priority_start += 11;
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

                                const char *parent_start = strstr(todo_obj, "\"parent_id\":");
                                if (parent_start) {
                                    parent_start += 12;
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

                                if (strlen(content) > 0) {
                                    TaskPriority priority = task_priority_from_string(priority_str);
                                    TaskStatus status = task_status_from_string(status_str);

                                    if (use_sqlite && bulk_tasks) {
                                        if (bulk_count >= bulk_capacity) {
                                            bulk_capacity *= 2;
                                            Task* new_tasks = realloc(bulk_tasks, sizeof(Task) * bulk_capacity);
                                            if (new_tasks) {
                                                bulk_tasks = new_tasks;
                                            }
                                        }

                                        if (bulk_count < bulk_capacity) {
                                            Task* task = &bulk_tasks[bulk_count];
                                            memset(task, 0, sizeof(Task));
                                            task->content = strdup(content);
                                            task->status = status;
                                            task->priority = priority;
                                            size_t parent_len = strlen(parent_id);
                                            if (parent_len > 0 && parent_len < sizeof(task->parent_id)) {
                                                memcpy(task->parent_id, parent_id, parent_len);
                                            }
                                            task->created_at = time(NULL);
                                            task->updated_at = task->created_at;
                                            bulk_count++;
                                        }
                                    } else {
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

                if (use_sqlite && bulk_tasks && bulk_count > 0) {
                    task_store_replace_session_tasks(store, GLOBAL_SESSION_ID, bulk_tasks, bulk_count);
                    sync_todolist_from_store();
                }

                if (bulk_tasks) {
                    for (size_t i = 0; i < bulk_count; i++) {
                        free(bulk_tasks[i].content);
                    }
                    free(bulk_tasks);
                }

                todo_display_update(g_todo_list);

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

void clear_todo_tool_reference(void) {
    g_todo_list = NULL;
}

int register_todo_tool(ToolRegistry* registry, TodoList* todo_list) {
    if (registry == NULL || todo_list == NULL) {
        return -1;
    }

    g_todo_list = todo_list;

    // Hydrate in-memory list from SQLite for cross-session persistence
    sync_todolist_from_store();

    ToolParameter parameters[1];
    memset(parameters, 0, sizeof(parameters));

    static const char* todo_items_schema =
        "{\"type\": \"object\", "
        "\"properties\": {"
            "\"id\": {\"type\": \"string\", \"description\": \"Unique identifier for the todo item\"},"
            "\"content\": {\"type\": \"string\", \"description\": \"The task description\"},"
            "\"status\": {\"type\": \"string\", \"enum\": [\"pending\", \"in_progress\", \"completed\"], \"description\": \"Current status of the task\"},"
            "\"priority\": {\"type\": \"string\", \"enum\": [\"low\", \"medium\", \"high\"], \"description\": \"Priority level\"}"
        "}, "
        "\"required\": [\"content\"]}";

    parameters[0].name = strdup("todos");
    parameters[0].type = strdup("array");
    parameters[0].description = strdup("Array of todo items. Each item must have 'content' (task description). Optional: 'id', 'status' (pending/in_progress/completed), 'priority' (low/medium/high)");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;
    parameters[0].items_schema = strdup(todo_items_schema);

    if (parameters[0].name == NULL ||
        parameters[0].type == NULL ||
        parameters[0].description == NULL ||
        parameters[0].items_schema == NULL) {
        free(parameters[0].name);
        free(parameters[0].type);
        free(parameters[0].description);
        free(parameters[0].items_schema);
        return -1;
    }
    
    int result = register_tool(registry, "TodoWrite",
                              "Write/replace the task list. Use for complex multi-step work requiring systematic tracking. Pass the complete list of tasks.",
                              parameters, 1, execute_todo_tool_call);

    free(parameters[0].name);
    free(parameters[0].type);
    free(parameters[0].description);
    free(parameters[0].items_schema);

    if (result != 0) {
        return result;
    }

    result = register_tool(registry, "TodoRead",
                          "Read the current task list. Use this to check what tasks exist before modifying them.",
                          NULL, 0, execute_todo_tool_call);

    return result;
}