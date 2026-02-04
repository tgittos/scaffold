/**
 * Verified File Context Module
 *
 * Provides a mechanism to pass verified file access from the approval gate
 * system to tool execution. This enables TOCTOU-safe file operations by
 * allowing tools (including Python-based tools) to use pre-verified file
 * descriptors instead of opening files directly.
 *
 * Usage flow:
 * 1. tool_executor.c captures ApprovedPath during gate approval
 * 2. Before tool execution, call verified_file_context_set() with the path
 * 3. Tool (C or Python) calls verified_file_context_get_fd() to get a
 *    verified file descriptor atomically
 * 4. Tool uses the fd (Python: os.fdopen(fd, mode))
 * 5. After tool execution, call verified_file_context_clear()
 *
 * Thread Safety:
 * This module uses thread-local storage, making it safe for concurrent
 * tool execution in different threads. Each thread has its own context.
 */

#ifndef VERIFIED_FILE_CONTEXT_H
#define VERIFIED_FILE_CONTEXT_H

#include "atomic_file.h"

/**
 * File open mode for verified file access.
 */
typedef enum {
    VERIFIED_MODE_READ,      /* Open for reading (O_RDONLY) */
    VERIFIED_MODE_WRITE,     /* Open for writing, create/truncate (O_WRONLY|O_CREAT|O_TRUNC) */
    VERIFIED_MODE_APPEND,    /* Open for appending (O_WRONLY|O_APPEND|O_CREAT) */
    VERIFIED_MODE_READWRITE  /* Open for read/write (O_RDWR) */
} VerifiedFileMode;

/**
 * Set the current verified file context for tool execution.
 *
 * Call this before executing a file-based tool after approval.
 * The approved path data will be used when the tool requests
 * a verified file descriptor.
 *
 * @param approved The approved path from gate approval (copied internally)
 * @return 0 on success, -1 on failure
 */
int verified_file_context_set(const ApprovedPath *approved);

/**
 * Clear the current verified file context.
 *
 * Call this after tool execution completes to release resources.
 */
void verified_file_context_clear(void);

/**
 * Check if a verified file context is currently active.
 *
 * @return 1 if context is set, 0 otherwise
 */
int verified_file_context_is_set(void);

/**
 * Get a verified file descriptor for the approved path.
 *
 * This function atomically verifies the path hasn't changed since approval
 * and opens it, returning a file descriptor. The tool should use this fd
 * instead of opening the path directly.
 *
 * @param requested_path The path the tool wants to access (must match approved path)
 * @param mode The access mode (read, write, append)
 * @param out_fd Output: file descriptor on success (-1 on failure)
 * @return VERIFY_OK on success, or specific error code
 */
VerifyResult verified_file_context_get_fd(const char *requested_path,
                                           VerifiedFileMode mode,
                                           int *out_fd);

/**
 * Get the resolved path from the current context.
 *
 * Useful for tools that need the canonical path after verification.
 *
 * @return The resolved path string, or NULL if no context is set.
 *         The returned pointer is valid until verified_file_context_clear().
 */
const char *verified_file_context_get_resolved_path(void);

/**
 * Check if a path matches the currently approved path.
 *
 * This is a convenience function that normalizes both paths and compares.
 *
 * @param requested_path The path to check
 * @return 1 if paths match (or resolve to same file), 0 otherwise
 */
int verified_file_context_path_matches(const char *requested_path);

#endif /* VERIFIED_FILE_CONTEXT_H */
