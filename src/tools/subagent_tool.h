#ifndef SUBAGENT_TOOL_H
#define SUBAGENT_TOOL_H

#include "tools_system.h"
#include "utils/darray.h"
#include "policy/approval_gate.h"
#include <sys/types.h>
#include <time.h>

/**
 * Configuration defaults for subagent system
 */
#define SUBAGENT_MAX_DEFAULT 5
#define SUBAGENT_TIMEOUT_DEFAULT 300
#define SUBAGENT_ID_LENGTH 16
#define SUBAGENT_MAX_OUTPUT_LENGTH 131072  // 128KB max output per subagent

/**
 * Internal constants for subagent operations
 */
#define SUBAGENT_PATH_BUFFER_SIZE 4096     // Buffer size for executable paths
#define SUBAGENT_POLL_INTERVAL_USEC 50000  // 50ms polling interval
#define SUBAGENT_GRACE_PERIOD_USEC 100000  // 100ms grace period before SIGKILL
#define SUBAGENT_HARD_CAP 20               // Absolute maximum concurrent subagents
#define SUBAGENT_MAX_TIMEOUT_SEC 3600      // Maximum timeout: 1 hour

/**
 * Subagent execution status enum
 */
typedef enum {
    SUBAGENT_STATUS_PENDING,    // Created but not yet started
    SUBAGENT_STATUS_RUNNING,    // Currently executing
    SUBAGENT_STATUS_COMPLETED,  // Completed successfully
    SUBAGENT_STATUS_FAILED,     // Failed with error
    SUBAGENT_STATUS_TIMEOUT     // Killed due to timeout
} SubagentStatus;

/**
 * Structure representing an individual subagent instance
 */
typedef struct {
    char id[SUBAGENT_ID_LENGTH + 1];  // Unique identifier (hex string)
    pid_t pid;                         // Process ID of spawned process
    SubagentStatus status;             // Current execution status
    int stdout_pipe[2];                // Pipe for reading child output
    ApprovalChannel approval_channel;  // IPC channel for approval proxying
    char *task;                        // Task description passed to subagent
    char *context;                     // Optional context information
    char *output;                      // Accumulated output from child
    size_t output_len;                 // Length of accumulated output
    char *result;                      // Final result (when completed)
    char *error;                       // Error message (when failed)
    time_t start_time;                 // Unix timestamp when spawned
} Subagent;

DARRAY_DECLARE(SubagentArray, Subagent)

/**
 * Manager structure for tracking all subagents
 */
typedef struct {
    SubagentArray subagents;           // Dynamic array of subagents
    int max_subagents;                 // Maximum allowed concurrent subagents
    int timeout_seconds;               // Timeout for each subagent execution
    int is_subagent_process;           // Flag: 1 if running as subagent (prevents nesting)
} SubagentManager;

/**
 * Initialize the subagent manager
 *
 * @param manager Pointer to SubagentManager structure to initialize
 * @return 0 on success, -1 on failure
 */
int subagent_manager_init(SubagentManager *manager);

/**
 * Initialize subagent manager with specific configuration values
 *
 * @param manager Pointer to SubagentManager structure to initialize
 * @param max_subagents Maximum concurrent subagents allowed
 * @param timeout_seconds Timeout for subagent execution
 * @return 0 on success, -1 on failure
 */
int subagent_manager_init_with_config(SubagentManager *manager, int max_subagents, int timeout_seconds);

/**
 * Clean up all subagent resources
 * Kills any running subagents and frees all memory
 *
 * @param manager Pointer to SubagentManager structure to cleanup
 */
void subagent_manager_cleanup(SubagentManager *manager);

/**
 * Spawn a new subagent to execute a task
 *
 * @param manager Pointer to SubagentManager
 * @param task Task description for the subagent to execute (required)
 * @param context Optional context information for the subagent
 * @param subagent_id_out Buffer to receive the generated subagent ID (must be at least SUBAGENT_ID_LENGTH + 1)
 * @return 0 on success, -1 on failure (e.g., max limit reached, fork failed)
 */
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out);

/**
 * Get the current status of a subagent
 *
 * @param manager Pointer to SubagentManager
 * @param subagent_id ID of the subagent to query
 * @param wait If non-zero, block until subagent completes
 * @param status Output: current status of the subagent
 * @param result Output: result string if completed (caller must free)
 * @param error Output: error string if failed (caller must free)
 * @return 0 on success, -1 if subagent not found
 */
int subagent_get_status(SubagentManager *manager, const char *subagent_id, int wait,
                        SubagentStatus *status, char **result, char **error);

/**
 * Poll all running subagents for status changes (non-blocking)
 * Updates internal state for any completed/failed subagents
 *
 * @param manager Pointer to SubagentManager
 * @return Number of subagents that changed state, or -1 on error
 */
int subagent_poll_all(SubagentManager *manager);

/**
 * Find a subagent by ID
 *
 * @param manager Pointer to SubagentManager
 * @param subagent_id ID to search for
 * @return Pointer to Subagent if found, NULL otherwise
 */
Subagent* subagent_find_by_id(SubagentManager *manager, const char *subagent_id);

/**
 * Convert subagent status enum to string representation
 *
 * @param status Subagent status enum value
 * @return Static string representing the status
 */
const char* subagent_status_to_string(SubagentStatus status);

/**
 * Register the subagent tool with the tool registry
 *
 * @param registry Pointer to ToolRegistry structure
 * @param manager Pointer to SubagentManager (stored for tool execution)
 * @return 0 on success, -1 on failure
 */
int register_subagent_tool(ToolRegistry *registry, SubagentManager *manager);

/**
 * Register the subagent_status tool with the tool registry
 *
 * @param registry Pointer to ToolRegistry structure
 * @param manager Pointer to SubagentManager (stored for tool execution)
 * @return 0 on success, -1 on failure
 */
int register_subagent_status_tool(ToolRegistry *registry, SubagentManager *manager);

/**
 * Execute the subagent tool call
 * Spawns a new subagent process to handle the given task
 *
 * @param tool_call Tool call structure from tools system
 * @param result Tool result structure for tools system
 * @return 0 on success, -1 on failure
 */
int execute_subagent_tool_call(const ToolCall *tool_call, ToolResult *result);

/**
 * Execute the subagent_status tool call
 * Queries the status of an existing subagent
 *
 * @param tool_call Tool call structure from tools system
 * @param result Tool result structure for tools system
 * @return 0 on success, -1 on failure
 */
int execute_subagent_status_tool_call(const ToolCall *tool_call, ToolResult *result);

/**
 * Entry point for running ralph as a subagent process
 * Called from main() when --subagent flag is present
 *
 * @param task Task description to execute
 * @param context Optional context information
 * @return Exit code (0 on success)
 */
int ralph_run_as_subagent(const char *task, const char *context);

/**
 * Generate a unique subagent ID
 * Uses /dev/urandom for randomness with time/pid fallback
 *
 * @param id_out Buffer to receive the ID (must be at least SUBAGENT_ID_LENGTH + 1)
 */
void generate_subagent_id(char *id_out);

/**
 * Read available output from a subagent's pipe (non-blocking)
 *
 * @param sub Pointer to the Subagent structure
 * @return Number of bytes read, or -1 on error
 */
int read_subagent_output_nonblocking(Subagent *sub);

/**
 * Read all remaining output from a subagent's pipe (blocking)
 * Called when subagent has exited to collect final output
 *
 * @param sub Pointer to the Subagent structure
 * @return 0 on success, -1 on error
 */
int read_subagent_output(Subagent *sub);

/**
 * Clean up resources for a single subagent
 *
 * @param sub Pointer to the Subagent structure to cleanup
 */
void cleanup_subagent(Subagent *sub);

/**
 * Poll all running subagents for pending approval requests.
 * Returns the index of the first subagent with a pending request.
 *
 * @param manager Pointer to SubagentManager
 * @param timeout_ms Maximum time to wait in milliseconds (0 for non-blocking)
 * @return Index of subagent with pending request, or -1 if none
 */
int subagent_poll_approval_requests(SubagentManager *manager, int timeout_ms);

/**
 * Handle an approval request from a specific subagent.
 * Prompts the user via TTY and sends the response back to the subagent.
 *
 * @param manager Pointer to SubagentManager
 * @param subagent_index Index of the subagent in the manager's array
 * @param gate_config Parent's approval gate configuration
 * @return 0 on success, -1 on error
 */
int subagent_handle_approval_request(SubagentManager *manager,
                                     int subagent_index,
                                     ApprovalGateConfig *gate_config);

/**
 * Check and handle any pending approval requests from subagents.
 * Non-blocking check that handles at most one request.
 *
 * @param manager Pointer to SubagentManager
 * @param gate_config Parent's approval gate configuration
 * @return 1 if a request was handled, 0 if none pending, -1 on error
 */
int subagent_check_and_handle_approvals(SubagentManager *manager,
                                        ApprovalGateConfig *gate_config);

/**
 * Get the approval channel for this subagent process.
 * Returns NULL if not running as a subagent.
 *
 * @return Pointer to approval channel, or NULL if not a subagent
 */
ApprovalChannel* subagent_get_approval_channel(void);

#endif // SUBAGENT_TOOL_H
