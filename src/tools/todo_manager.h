#ifndef TODO_MANAGER_H
#define TODO_MANAGER_H

#include <stddef.h>
#include <time.h>
#include "utils/darray.h"

#define TODO_MAX_CONTENT_LENGTH 512
#define TODO_MAX_ID_LENGTH 64
#define TODO_MAX_COUNT 100

typedef enum {
    TODO_STATUS_PENDING,
    TODO_STATUS_IN_PROGRESS,
    TODO_STATUS_COMPLETED
} TodoStatus;

typedef enum {
    TODO_PRIORITY_LOW = 1,
    TODO_PRIORITY_MEDIUM = 2,
    TODO_PRIORITY_HIGH = 3
} TodoPriority;

typedef struct {
    char id[TODO_MAX_ID_LENGTH];
    char content[TODO_MAX_CONTENT_LENGTH];
    TodoStatus status;
    TodoPriority priority;
    time_t created_at;
    time_t updated_at;
} Todo;

DARRAY_DECLARE(TodoList, Todo)

int todo_list_init(TodoList* list);
void todo_list_destroy(TodoList* list);

int todo_create(TodoList* list, const char* content, TodoPriority priority, char* out_id);
int todo_update_status(TodoList* list, const char* id, TodoStatus status);
int todo_update_priority(TodoList* list, const char* id, TodoPriority priority);
int todo_delete(TodoList* list, const char* id);

Todo* todo_find_by_id(TodoList* list, const char* id);
int todo_list_filter(TodoList* list, int status_filter, TodoPriority min_priority, Todo** out_todos, size_t* out_count);

char* todo_serialize_json(TodoList* list);
int todo_deserialize_json(TodoList* list, const char* json_data);

const char* todo_status_to_string(TodoStatus status);
TodoStatus todo_status_from_string(const char* status_str);
const char* todo_priority_to_string(TodoPriority priority);
TodoPriority todo_priority_from_string(const char* priority_str);

// Check if there are any pending or in-progress todos
int todo_has_pending_tasks(TodoList* list);

#endif