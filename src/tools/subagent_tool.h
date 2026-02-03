#ifndef SUBAGENT_TOOL_H
#define SUBAGENT_TOOL_H

#include "tools_system.h"
#include "util/darray.h"
#include "policy/approval_gate.h"
#include <sys/types.h>
#include <time.h>

#define SUBAGENT_MAX_DEFAULT 5
#define SUBAGENT_TIMEOUT_DEFAULT 300
#define SUBAGENT_ID_LENGTH 16
#define SUBAGENT_MAX_OUTPUT_LENGTH 131072  // 128KB max output per subagent

#define SUBAGENT_PATH_BUFFER_SIZE 4096     // Buffer size for executable paths
#define SUBAGENT_POLL_INTERVAL_USEC 50000  // 50ms polling interval
#define SUBAGENT_GRACE_PERIOD_USEC 100000  // 100ms grace period before SIGKILL
#define SUBAGENT_HARD_CAP 20               // Absolute maximum concurrent subagents
#define SUBAGENT_MAX_TIMEOUT_SEC 3600      // Maximum timeout: 1 hour

typedef enum {
    SUBAGENT_STATUS_PENDING,    // Created but not yet started
    SUBAGENT_STATUS_RUNNING,    // Currently executing
    SUBAGENT_STATUS_COMPLETED,  // Completed successfully
    SUBAGENT_STATUS_FAILED,     // Failed with error
    SUBAGENT_STATUS_TIMEOUT     // Killed due to timeout
} SubagentStatus;

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

typedef struct {
    SubagentArray subagents;           // Dynamic array of subagents
    int max_subagents;                 // Maximum allowed concurrent subagents
    int timeout_seconds;               // Timeout for each subagent execution
    int is_subagent_process;           // Flag: 1 if running as subagent (prevents nesting)
    ApprovalGateConfig *gate_config;   // Parent's approval config for proxying requests
} SubagentManager;

int subagent_manager_init(SubagentManager *manager);
int subagent_manager_init_with_config(SubagentManager *manager, int max_subagents, int timeout_seconds);

/** Set the approval gate config for proxying subagent approval requests. */
void subagent_manager_set_gate_config(SubagentManager *manager, ApprovalGateConfig *gate_config);

/** Kills any running subagents and frees all memory. */
void subagent_manager_cleanup(SubagentManager *manager);

/** subagent_id_out must be at least SUBAGENT_ID_LENGTH + 1 bytes. */
int subagent_spawn(SubagentManager *manager, const char *task, const char *context, char *subagent_id_out);

/** If wait is non-zero, blocks until subagent completes. Caller must free result/error. */
int subagent_get_status(SubagentManager *manager, const char *subagent_id, int wait,
                        SubagentStatus *status, char **result, char **error);

/** Non-blocking poll. Returns number of subagents that changed state. */
int subagent_poll_all(SubagentManager *manager);

Subagent* subagent_find_by_id(SubagentManager *manager, const char *subagent_id);
const char* subagent_status_to_string(SubagentStatus status);

int register_subagent_tool(ToolRegistry *registry, SubagentManager *manager);
int register_subagent_status_tool(ToolRegistry *registry, SubagentManager *manager);

int execute_subagent_tool_call(const ToolCall *tool_call, ToolResult *result);
int execute_subagent_status_tool_call(const ToolCall *tool_call, ToolResult *result);

/** Uses /dev/urandom with time/pid fallback. id_out must be >= SUBAGENT_ID_LENGTH + 1. */
void generate_subagent_id(char *id_out);

int read_subagent_output_nonblocking(Subagent *sub);

/** Blocking read of remaining output after subagent exit. */
int read_subagent_output(Subagent *sub);

void cleanup_subagent(Subagent *sub);

/** Returns index of first subagent with a pending approval request, or -1. */
int subagent_poll_approval_requests(SubagentManager *manager, int timeout_ms);

/** Prompts user via TTY and sends response back to subagent. */
int subagent_handle_approval_request(SubagentManager *manager,
                                     int subagent_index,
                                     ApprovalGateConfig *gate_config);

/** Non-blocking; handles at most one request. Returns 1 if handled, 0 if none, -1 on error. */
int subagent_check_and_handle_approvals(SubagentManager *manager,
                                        ApprovalGateConfig *gate_config);

/** Returns NULL if not running as a subagent process. */
ApprovalChannel* subagent_get_approval_channel(void);

/** Initialize approval channel from environment variables (for subagent processes). */
int subagent_init_approval_channel(void);

/** Clean up approval channel resources. */
void subagent_cleanup_approval_channel(void);

#endif // SUBAGENT_TOOL_H
