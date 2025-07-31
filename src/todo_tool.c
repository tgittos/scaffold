#include "todo_tool.h"
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