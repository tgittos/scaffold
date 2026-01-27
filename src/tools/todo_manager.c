#include "todo_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

DARRAY_DEFINE(TodoList, Todo)

char* todo_serialize_json(TodoList* list) {
    if (!list) return NULL;

    // Each todo entry can be up to ~450 bytes when serialized:
    // - UUID id: 36 chars, content: up to 256 chars (%.256s), status/priority: ~25 chars
    // - timestamps: ~20 chars, JSON overhead: ~80 chars, comma: 1 char
    // Using 512 per entry for safety margin
    size_t buffer_size = 1024 + (list->count * 512);
    char* json = malloc(buffer_size);
    if (!json) return NULL;

    strcpy(json, "{\"todos\":[");

    for (size_t i = 0; i < list->count; i++) {
        Todo* todo = &list->data[i];
        char todo_json[1024] = {0};

        snprintf(todo_json, sizeof(todo_json),
            "%s{\"id\":\"%s\",\"content\":\"%.256s\",\"status\":\"%s\",\"priority\":\"%s\",\"created_at\":%ld,\"updated_at\":%ld}",
            (i > 0) ? "," : "",
            todo->id,
            todo->content,
            todo_status_to_string(todo->status),
            todo_priority_to_string(todo->priority),
            todo->created_at,
            todo->updated_at
        );

        strcat(json, todo_json);
    }

    strcat(json, "]}");
    return json;
}

int todo_list_init(TodoList* list) {
    if (!list) return -1;
    return TodoList_init_capacity(list, 10);
}

void todo_list_destroy(TodoList* list) {
    if (!list) return;
    TodoList_destroy(list);
}

static void generate_todo_id(char* out_id) {
    static int counter = 0;
    snprintf(out_id, TODO_MAX_ID_LENGTH, "todo_%d", ++counter);
}

int todo_create(TodoList* list, const char* content, TodoPriority priority, char* out_id) {
    if (!list || !content || !out_id) return -1;
    if (strlen(content) >= TODO_MAX_CONTENT_LENGTH) return -1;

    if (list->count >= TODO_MAX_COUNT) return -1;

    Todo new_todo;
    generate_todo_id(new_todo.id);
    strncpy(new_todo.content, content, TODO_MAX_CONTENT_LENGTH - 1);
    new_todo.content[TODO_MAX_CONTENT_LENGTH - 1] = '\0';
    new_todo.status = TODO_STATUS_PENDING;
    new_todo.priority = priority;
    new_todo.created_at = time(NULL);
    new_todo.updated_at = new_todo.created_at;

    if (TodoList_push(list, new_todo) != 0) return -1;

    strncpy(out_id, new_todo.id, TODO_MAX_ID_LENGTH - 1);
    out_id[TODO_MAX_ID_LENGTH - 1] = '\0';

    return 0;
}

Todo* todo_find_by_id(TodoList* list, const char* id) {
    if (!list || !id) return NULL;

    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->data[i].id, id) == 0) {
            return &list->data[i];
        }
    }
    return NULL;
}

int todo_update_status(TodoList* list, const char* id, TodoStatus status) {
    if (!list || !id) return -1;
    
    Todo* todo = todo_find_by_id(list, id);
    if (!todo) return -1;
    
    todo->status = status;
    todo->updated_at = time(NULL);
    return 0;
}

int todo_update_priority(TodoList* list, const char* id, TodoPriority priority) {
    if (!list || !id) return -1;
    
    Todo* todo = todo_find_by_id(list, id);
    if (!todo) return -1;
    
    todo->priority = priority;
    todo->updated_at = time(NULL);
    return 0;
}

int todo_delete(TodoList* list, const char* id) {
    if (!list || !id) return -1;

    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->data[i].id, id) == 0) {
            return TodoList_remove(list, i);
        }
    }
    return -1;
}

int todo_list_filter(TodoList* list, int status_filter, TodoPriority min_priority,
                    Todo** out_todos, size_t* out_count) {
    if (!list || !out_todos || !out_count) return -1;

    *out_todos = malloc(sizeof(Todo) * list->count);
    if (!*out_todos) return -1;

    *out_count = 0;
    for (size_t i = 0; i < list->count; i++) {
        Todo* todo = &list->data[i];
        if ((status_filter < 0 || todo->status == (TodoStatus)status_filter) &&
            todo->priority >= min_priority) {
            (*out_todos)[*out_count] = *todo;
            (*out_count)++;
        }
    }

    return 0;
}

const char* todo_status_to_string(TodoStatus status) {
    switch (status) {
        case TODO_STATUS_PENDING: return "pending";
        case TODO_STATUS_IN_PROGRESS: return "in_progress";
        case TODO_STATUS_COMPLETED: return "completed";
        default: return "unknown";
    }
}

TodoStatus todo_status_from_string(const char* status_str) {
    if (!status_str) return TODO_STATUS_PENDING;
    
    if (strcmp(status_str, "pending") == 0) return TODO_STATUS_PENDING;
    if (strcmp(status_str, "in_progress") == 0) return TODO_STATUS_IN_PROGRESS;
    if (strcmp(status_str, "completed") == 0) return TODO_STATUS_COMPLETED;
    
    return TODO_STATUS_PENDING;
}

const char* todo_priority_to_string(TodoPriority priority) {
    switch (priority) {
        case TODO_PRIORITY_LOW: return "low";
        case TODO_PRIORITY_MEDIUM: return "medium";
        case TODO_PRIORITY_HIGH: return "high";
        default: return "low";
    }
}

TodoPriority todo_priority_from_string(const char* priority_str) {
    if (!priority_str) return TODO_PRIORITY_LOW;
    
    if (strcmp(priority_str, "low") == 0) return TODO_PRIORITY_LOW;
    if (strcmp(priority_str, "medium") == 0) return TODO_PRIORITY_MEDIUM;
    if (strcmp(priority_str, "high") == 0) return TODO_PRIORITY_HIGH;
    
    return TODO_PRIORITY_LOW;
}

int todo_has_pending_tasks(TodoList* list) {
    if (list == NULL || list->data == NULL) {
        return 0;
    }

    for (size_t i = 0; i < list->count; i++) {
        if (list->data[i].status == TODO_STATUS_PENDING ||
            list->data[i].status == TODO_STATUS_IN_PROGRESS) {
            return 1;
        }
    }

    return 0;
}
