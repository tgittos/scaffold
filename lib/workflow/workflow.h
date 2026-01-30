/**
 * lib/workflow/workflow.h - Workflow and task queue abstractions
 *
 * Provides higher-level workflow primitives built on top of the task store
 * and message store. This module enables orchestrator agents to manage
 * work queues and coordinate worker agents.
 *
 * Key concepts:
 * - WorkQueue: A named queue of tasks that workers can claim
 * - WorkItem: A task in a queue with assignment tracking
 * - WorkerHandle: Reference to a spawned worker agent
 */

#ifndef LIB_WORKFLOW_WORKFLOW_H
#define LIB_WORKFLOW_WORKFLOW_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* Re-export task store types */
#include "../../src/db/task_store.h"
#include "../../src/db/message_store.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * WORK ITEM
 * ============================================================================= */

/**
 * Status of a work item in a queue.
 */
typedef enum WorkItemStatus {
    WORK_ITEM_PENDING = 0,      /**< Waiting to be claimed */
    WORK_ITEM_ASSIGNED = 1,     /**< Claimed by a worker */
    WORK_ITEM_COMPLETED = 2,    /**< Successfully completed */
    WORK_ITEM_FAILED = 3        /**< Failed after attempts exhausted */
} WorkItemStatus;

/**
 * A work item in a queue.
 */
typedef struct WorkItem {
    char id[40];                 /**< Unique work item ID */
    char queue_name[64];         /**< Queue this item belongs to */
    char* task_description;      /**< Description of work to be done */
    char* context;               /**< Additional context (JSON or text) */
    char assigned_to[64];        /**< Worker agent ID if assigned */
    WorkItemStatus status;
    int attempt_count;           /**< Number of times this was attempted */
    int max_attempts;            /**< Maximum retry attempts */
    time_t created_at;
    time_t assigned_at;
    time_t completed_at;
    char* result;                /**< Result from worker (on completion) */
    char* error;                 /**< Error message (on failure) */
} WorkItem;

/* =============================================================================
 * WORK QUEUE
 * ============================================================================= */

/**
 * A named queue for distributing work to agents.
 */
typedef struct WorkQueue WorkQueue;

/**
 * Create or get a work queue by name.
 * Queues are persistent across sessions.
 *
 * @param name Queue name (max 63 chars)
 * @return Queue handle, or NULL on error. Call work_queue_destroy when done.
 */
WorkQueue* work_queue_create(const char* name);

/**
 * Destroy a queue handle (does not delete the queue contents).
 *
 * @param queue Queue to destroy
 */
void work_queue_destroy(WorkQueue* queue);

/**
 * Enqueue a new work item.
 *
 * @param queue Target queue
 * @param task_description Description of work
 * @param context Additional context (may be NULL)
 * @param max_attempts Maximum retry attempts (0 for default of 3)
 * @param out_id Output: work item ID (at least 40 bytes)
 * @return 0 on success, -1 on error
 */
int work_queue_enqueue(WorkQueue* queue, const char* task_description,
                       const char* context, int max_attempts, char* out_id);

/**
 * Claim the next available work item for a worker.
 * The item is marked as assigned to the worker.
 *
 * @param queue Queue to claim from
 * @param worker_id ID of worker claiming the work
 * @return Work item (caller owns and must free), or NULL if queue empty
 */
WorkItem* work_queue_claim(WorkQueue* queue, const char* worker_id);

/**
 * Report completion of a work item.
 *
 * @param queue Queue containing the item
 * @param item_id Work item ID
 * @param result Result to store (may be NULL)
 * @return 0 on success, -1 on error
 */
int work_queue_complete(WorkQueue* queue, const char* item_id, const char* result);

/**
 * Report failure of a work item.
 * If attempts remain, the item is returned to pending status.
 *
 * @param queue Queue containing the item
 * @param item_id Work item ID
 * @param error Error message
 * @return 0 on success (item requeued or marked failed), -1 on error
 */
int work_queue_fail(WorkQueue* queue, const char* item_id, const char* error);

/**
 * Get the number of pending items in a queue.
 *
 * @param queue Queue to check
 * @return Number of pending items, or -1 on error
 */
int work_queue_pending_count(WorkQueue* queue);

/**
 * Free a work item.
 *
 * @param item Item to free
 */
void work_item_free(WorkItem* item);

/* =============================================================================
 * WORKER MANAGEMENT
 * ============================================================================= */

/**
 * Handle to a spawned worker agent.
 */
typedef struct WorkerHandle {
    char agent_id[64];           /**< Worker's agent ID */
    char queue_name[64];         /**< Queue the worker is processing */
    pid_t pid;                   /**< Process ID of worker */
    bool is_running;             /**< Whether worker is still active */
} WorkerHandle;

/**
 * Spawn a worker agent to process items from a queue.
 *
 * @param queue_name Queue for the worker to process
 * @param system_prompt System prompt for the worker (may be NULL for default)
 * @return Worker handle (caller owns and must free), or NULL on error
 */
WorkerHandle* worker_spawn(const char* queue_name, const char* system_prompt);

/**
 * Check if a worker is still running.
 *
 * @param handle Worker handle
 * @return true if running, false if terminated
 */
bool worker_is_running(WorkerHandle* handle);

/**
 * Stop a worker agent.
 *
 * @param handle Worker handle
 * @return 0 on success, -1 on error
 */
int worker_stop(WorkerHandle* handle);

/**
 * Free a worker handle.
 *
 * @param handle Handle to free
 */
void worker_handle_free(WorkerHandle* handle);

#ifdef __cplusplus
}
#endif

#endif /* LIB_WORKFLOW_WORKFLOW_H */
