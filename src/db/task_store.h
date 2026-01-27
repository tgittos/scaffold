#ifndef TASK_STORE_H
#define TASK_STORE_H

#include <stddef.h>
#include <time.h>
#include "../utils/ptrarray.h"

// Values match TodoStatus for compatibility with the tool API.
typedef enum {
    TASK_STATUS_PENDING = 0,
    TASK_STATUS_IN_PROGRESS = 1,
    TASK_STATUS_COMPLETED = 2
} TaskStatus;

// Values match TodoPriority for compatibility with the tool API.
typedef enum {
    TASK_PRIORITY_LOW = 1,
    TASK_PRIORITY_MEDIUM = 2,
    TASK_PRIORITY_HIGH = 3
} TaskPriority;

typedef struct {
    char id[40];
    char session_id[40];
    char parent_id[40];       // Empty string if root task
    char* content;
    TaskStatus status;
    TaskPriority priority;
    time_t created_at;
    time_t updated_at;
    // Dependency arrays are only populated by explicit query functions, not by default.
    char** blocked_by_ids;
    size_t blocked_by_count;
    char** blocks_ids;
    size_t blocks_count;
} Task;

PTRARRAY_DECLARE(TaskArray, Task)

typedef struct task_store task_store_t;

task_store_t* task_store_get_instance(void);
void task_store_destroy(task_store_t* store);
void task_store_reset_instance(void);
task_store_t* task_store_create(const char* db_path);

// out_id must be at least 40 bytes. Pass NULL parent_id for root tasks.
int task_store_create_task(task_store_t* store, const char* session_id,
                           const char* content, TaskPriority priority,
                           const char* parent_id,
                           char* out_id);

// Caller owns returned Task and must free with task_free().
Task* task_store_get_task(task_store_t* store, const char* id);

int task_store_update_status(task_store_t* store, const char* id, TaskStatus status);
int task_store_update_content(task_store_t* store, const char* id, const char* content);
int task_store_update_priority(task_store_t* store, const char* id, TaskPriority priority);

// Deletion cascades to children and removes dependency edges.
int task_store_delete_task(task_store_t* store, const char* id);

// Caller owns returned arrays and must free with task_free_list().
Task** task_store_get_children(task_store_t* store, const char* parent_id, size_t* count);
Task** task_store_get_subtree(task_store_t* store, const char* root_id, size_t* count);

int task_store_set_parent(task_store_t* store, const char* task_id, const char* parent_id);

// Returns -2 if the dependency would create a cycle.
int task_store_add_dependency(task_store_t* store, const char* task_id, const char* blocked_by_id);
int task_store_remove_dependency(task_store_t* store, const char* task_id, const char* blocked_by_id);

// Caller owns returned arrays and must free with task_free_id_list().
char** task_store_get_blockers(task_store_t* store, const char* task_id, size_t* count);
char** task_store_get_blocking(task_store_t* store, const char* task_id, size_t* count);

// Returns 1 if blocked, 0 if not, -1 on error.
int task_store_is_blocked(task_store_t* store, const char* task_id);

// Pass status_filter=-1 to include all statuses. Caller frees with task_free_list().
Task** task_store_list_by_session(task_store_t* store, const char* session_id,
                                   int status_filter, size_t* count);
Task** task_store_list_roots(task_store_t* store, const char* session_id, size_t* count);
Task** task_store_list_ready(task_store_t* store, const char* session_id, size_t* count);

// Returns 1 if has pending tasks, 0 if not, -1 on error.
int task_store_has_pending(task_store_t* store, const char* session_id);

// Atomically replaces all tasks for a session (delete + insert in a transaction).
int task_store_replace_session_tasks(task_store_t* store, const char* session_id,
                                      Task* tasks, size_t count);

void task_free(Task* task);
void task_free_list(Task** tasks, size_t count);
void task_free_id_list(char** ids, size_t count);

const char* task_status_to_string(TaskStatus status);
TaskStatus task_status_from_string(const char* status_str);
const char* task_priority_to_string(TaskPriority priority);
TaskPriority task_priority_from_string(const char* priority_str);

#endif // TASK_STORE_H
