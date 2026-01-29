#ifndef ASYNC_EXECUTOR_H
#define ASYNC_EXECUTOR_H

#include "ralph.h"

/**
 * Async executor module for non-blocking message processing.
 *
 * Moves ralph_process_message() to a background thread that communicates
 * with the main select() loop via pipe notifications. This allows users
 * to continue typing while tools and LLM calls execute.
 *
 * Thread safety: Only one execution at a time. The executor thread "owns"
 * the session while running; user input is queued until completion.
 */

typedef struct async_executor async_executor_t;

/**
 * Event types sent through the notification pipe.
 * Main loop reads these to determine what action to take.
 */
typedef enum {
    ASYNC_EVENT_COMPLETE = 'C',    /* Execution completed successfully */
    ASYNC_EVENT_ERROR = 'E',       /* Execution failed with error */
    ASYNC_EVENT_APPROVAL = 'A',    /* Approval needed from user */
    ASYNC_EVENT_INTERRUPTED = 'I'  /* Execution interrupted by Ctrl+C */
} AsyncEventType;

/**
 * Create a new async executor bound to a session.
 *
 * @param session The RalphSession to use for message processing.
 * @return New executor, or NULL on failure. Caller must destroy with async_executor_destroy.
 */
async_executor_t* async_executor_create(RalphSession* session);

/**
 * Destroy an async executor and free resources.
 * Cancels any running execution before destroying.
 *
 * @param executor Executor to destroy, may be NULL.
 */
void async_executor_destroy(async_executor_t* executor);

/**
 * Start asynchronous execution of a user message.
 * Returns immediately; execution happens in background thread.
 *
 * @param executor The executor to use.
 * @param message User message to process.
 * @return 0 on success, -1 if already running or on error.
 */
int async_executor_start(async_executor_t* executor, const char* message);

/**
 * Get the notification pipe fd for select().
 * When this fd is readable, call async_executor_process_events().
 *
 * @param executor The executor.
 * @return Read fd for notification pipe, or -1 if invalid.
 */
int async_executor_get_notify_fd(async_executor_t* executor);

/**
 * Process pending events from the executor.
 * Call this when the notify fd is readable in the select loop.
 *
 * @param executor The executor.
 * @return The event type processed, or -1 on error.
 */
int async_executor_process_events(async_executor_t* executor);

/**
 * Check if an execution is currently running.
 *
 * @param executor The executor.
 * @return 1 if running, 0 if idle.
 */
int async_executor_is_running(async_executor_t* executor);

/**
 * Request cancellation of the current execution.
 * The executor thread will stop at the next safe point.
 * Non-blocking; returns immediately.
 *
 * @param executor The executor to cancel.
 */
void async_executor_cancel(async_executor_t* executor);

/**
 * Wait for the current execution to complete.
 * Blocks until the executor thread finishes.
 *
 * @param executor The executor.
 * @return 0 on success, -1 on error or timeout.
 */
int async_executor_wait(async_executor_t* executor);

/**
 * Get the last error message from a failed execution.
 *
 * @param executor The executor.
 * @return Error message, or NULL if no error. Do not free.
 */
const char* async_executor_get_error(async_executor_t* executor);

/**
 * Get the result status of the last completed execution.
 *
 * @param executor The executor.
 * @return 0 for success, negative for various error types.
 */
int async_executor_get_result(async_executor_t* executor);

#endif /* ASYNC_EXECUTOR_H */
