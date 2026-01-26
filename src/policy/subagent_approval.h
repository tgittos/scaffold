/**
 * Subagent Approval Proxy Header
 *
 * Provides IPC-based approval proxying for subagents to prevent deadlocks
 * when both parent and subagent need user approval via TTY.
 *
 * The core data structures (ApprovalChannel, ApprovalRequest, ApprovalResponse)
 * and primary functions (subagent_request_approval, handle_subagent_approval_request,
 * free_approval_channel) are declared in approval_gate.h.
 *
 * This header provides additional utilities for pipe management and the
 * parent approval loop.
 */

#ifndef SUBAGENT_APPROVAL_H
#define SUBAGENT_APPROVAL_H

#include "approval_gate.h"
#include <sys/types.h>

/* =============================================================================
 * Pipe Creation for Subagent Spawning
 * ========================================================================== */

/**
 * Create approval channel pipes for a new subagent.
 *
 * Creates two pipes:
 * - request_pipe: subagent writes, parent reads
 * - response_pipe: parent writes, subagent reads
 *
 * After fork:
 * - Child calls setup_subagent_channel_child() to get its channel
 * - Parent calls setup_subagent_channel_parent() to get its channel
 *
 * @param request_pipe Output: pipe for requests [0]=read, [1]=write
 * @param response_pipe Output: pipe for responses [0]=read, [1]=write
 * @return 0 on success, -1 on error
 */
int create_approval_channel_pipes(int request_pipe[2], int response_pipe[2]);

/**
 * Set up the approval channel for the subagent (child) process.
 *
 * Closes parent ends of pipes and initializes channel struct.
 * Call this in the child process after fork().
 *
 * @param channel Output: channel struct to initialize
 * @param request_pipe The request pipe from create_approval_channel_pipes()
 * @param response_pipe The response pipe from create_approval_channel_pipes()
 */
void setup_subagent_channel_child(ApprovalChannel *channel,
                                  int request_pipe[2],
                                  int response_pipe[2]);

/**
 * Set up the approval channel for the parent process.
 *
 * Closes child ends of pipes and initializes channel struct.
 * Call this in the parent process after fork().
 *
 * @param channel Output: channel struct to initialize
 * @param request_pipe The request pipe from create_approval_channel_pipes()
 * @param response_pipe The response pipe from create_approval_channel_pipes()
 * @param child_pid The PID of the child process
 */
void setup_subagent_channel_parent(ApprovalChannel *channel,
                                   int request_pipe[2],
                                   int response_pipe[2],
                                   pid_t child_pid);

/**
 * Close all pipe ends and clean up after failed fork/spawn.
 *
 * @param request_pipe The request pipe
 * @param response_pipe The response pipe
 */
void cleanup_approval_channel_pipes(int request_pipe[2], int response_pipe[2]);

/* =============================================================================
 * Parent Approval Loop Support
 * ========================================================================== */

/**
 * Check if any subagent has a pending approval request.
 *
 * Uses poll() to check if data is available on any subagent request pipe.
 * This is a non-blocking check suitable for integration into a main loop.
 *
 * @param channels Array of approval channels for active subagents
 * @param channel_count Number of channels in the array
 * @param timeout_ms Maximum time to wait in milliseconds (0 for non-blocking)
 * @return Index of channel with pending request, or -1 if none (or error)
 */
int poll_subagent_approval_requests(ApprovalChannel *channels,
                                    int channel_count,
                                    int timeout_ms);

/**
 * Run the parent approval loop.
 *
 * Monitors all subagent request pipes using poll().
 * Handles interleaved approvals from multiple concurrent subagents.
 *
 * This function runs continuously until:
 * - All subagent channels are closed
 * - An error occurs
 * - The timeout expires
 *
 * @param config Parent's gate configuration
 * @param channels Array of approval channels for active subagents
 * @param channel_count Number of channels in the array
 * @param timeout_ms Maximum time to run the loop (0 for indefinite)
 * @return 0 on normal exit, -1 on error
 */
int parent_approval_loop(ApprovalGateConfig *config,
                         ApprovalChannel *channels,
                         int channel_count,
                         int timeout_ms);

#endif /* SUBAGENT_APPROVAL_H */
