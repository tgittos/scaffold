#ifndef TASK_STORE_H
#define TASK_STORE_H

#include <stddef.h>
#include <time.h>
#include "../utils/ptrarray.h"

// Task status values (matches TodoStatus for compatibility)
typedef enum {
    TASK_STATUS_PENDING = 0,
    TASK_STATUS_IN_PROGRESS = 1,
    TASK_STATUS_COMPLETED = 2
} TaskStatus;

// Task priority values (matches TodoPriority for compatibility)
typedef enum {
    TASK_PRIORITY_LOW = 1,
    TASK_PRIORITY_MEDIUM = 2,
    TASK_PRIORITY_HIGH = 3
} TaskPriority;

// Task structure with dynamic content and relationships
typedef struct {
    char id[40];              // UUID v4 (36 chars + null)
    char session_id[40];      // UUID v4 of owning session
    char parent_id[40];       // UUID of parent task (empty string if root)
    char* content;            // Dynamically allocated content (no size limit)
    TaskStatus status;
    TaskPriority priority;
    time_t created_at;
    time_t updated_at;
    // Dependency info (populated by queries when requested)
    char** blocked_by_ids;    // Array of task IDs this depends on
    size_t blocked_by_count;
    char** blocks_ids;        // Array of task IDs blocked by this
    size_t blocks_count;
} Task;

PTRARRAY_DECLARE(TaskArray, Task)

// Opaque task store type
typedef struct task_store task_store_t;

// Lifecycle (singleton pattern)
task_store_t* task_store_get_instance(void);
void task_store_destroy(task_store_t* store);
void task_store_reset_instance(void);  // For testing - resets singleton

// Initialize task store with custom path (for testing)
// Returns NULL on failure, store pointer on success
task_store_t* task_store_create(const char* db_path);

// CRUD operations
// Create a task, returns 0 on success, -1 on failure
// out_id must be at least 40 bytes to receive the generated UUID
int task_store_create_task(task_store_t* store, const char* session_id,
                           const char* content, TaskPriority priority,
                           const char* parent_id,  // NULL for root task
                           char* out_id);

// Get a task by ID, returns newly allocated Task or NULL if not found
// Caller must free with task_free()
Task* task_store_get_task(task_store_t* store, const char* id);

// Update task status
int task_store_update_status(task_store_t* store, const char* id, TaskStatus status);

// Update task content
int task_store_update_content(task_store_t* store, const char* id, const char* content);

// Update task priority
int task_store_update_priority(task_store_t* store, const char* id, TaskPriority priority);

// Delete a task (cascades to children and removes from dependencies)
int task_store_delete_task(task_store_t* store, const char* id);

// Parent/Child operations
// Get direct children of a task, returns array of Task pointers
// Caller must free with task_free_list()
Task** task_store_get_children(task_store_t* store, const char* parent_id, size_t* count);

// Get all descendants (recursive subtree), returns array of Task pointers
// Caller must free with task_free_list()
Task** task_store_get_subtree(task_store_t* store, const char* root_id, size_t* count);

// Set parent of a task (move task under different parent)
int task_store_set_parent(task_store_t* store, const char* task_id, const char* parent_id);

// Dependency operations
// Add dependency: task_id is blocked by blocked_by_id
// Returns 0 on success, -1 on failure, -2 if would create circular dependency
int task_store_add_dependency(task_store_t* store, const char* task_id, const char* blocked_by_id);

// Remove dependency
int task_store_remove_dependency(task_store_t* store, const char* task_id, const char* blocked_by_id);

// Get IDs of tasks that block the given task
// Caller must free with task_free_id_list()
char** task_store_get_blockers(task_store_t* store, const char* task_id, size_t* count);

// Get IDs of tasks that are blocked by the given task
// Caller must free with task_free_id_list()
char** task_store_get_blocking(task_store_t* store, const char* task_id, size_t* count);

// Check if a task is blocked (has incomplete blocking tasks)
// Returns 1 if blocked, 0 if not blocked, -1 on error
int task_store_is_blocked(task_store_t* store, const char* task_id);

// Query operations
// List tasks by session, optionally filtered by status (-1 for all statuses)
// Caller must free with task_free_list()
Task** task_store_list_by_session(task_store_t* store, const char* session_id,
                                   int status_filter, size_t* count);

// List only root tasks (no parent) for a session
// Caller must free with task_free_list()
Task** task_store_list_roots(task_store_t* store, const char* session_id, size_t* count);

// List tasks that are pending AND not blocked (ready to work on)
// Caller must free with task_free_list()
Task** task_store_list_ready(task_store_t* store, const char* session_id, size_t* count);

// Check if session has any pending or in-progress tasks
// Returns 1 if has pending tasks, 0 if not, -1 on error
int task_store_has_pending(task_store_t* store, const char* session_id);

// Bulk operations (for TodoWrite semantics)
// Replace all tasks for a session with new set
// This deletes existing tasks and inserts new ones in a transaction
int task_store_replace_session_tasks(task_store_t* store, const char* session_id,
                                      Task* tasks, size_t count);

// Memory management
void task_free(Task* task);
void task_free_list(Task** tasks, size_t count);
void task_free_id_list(char** ids, size_t count);

// Status/Priority conversion helpers
const char* task_status_to_string(TaskStatus status);
TaskStatus task_status_from_string(const char* status_str);
const char* task_priority_to_string(TaskPriority priority);
TaskPriority task_priority_from_string(const char* priority_str);

#endif // TASK_STORE_H
