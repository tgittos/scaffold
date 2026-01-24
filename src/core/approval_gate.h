#ifndef APPROVAL_GATE_H
#define APPROVAL_GATE_H

#include <regex.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Include tools_system.h for ToolCall definition */
#include "../tools/tools_system.h"

/**
 * Approval Gates Module
 *
 * Require user confirmation before executing potentially destructive operations.
 * This module provides:
 * - Category-based tool classification and gating
 * - Allowlist pattern matching (regex for general tools, parsed commands for shell)
 * - Protected file detection and blocking
 * - Denial rate limiting to prevent approval fatigue
 * - Subagent approval proxying via IPC
 * - TOCTOU-safe path verification
 *
 * Related headers (for detailed implementations):
 * - shell_parser.h: Shell command parsing and dangerous pattern detection
 * - protected_files.h: Protected file detection and inode caching
 * - path_normalize.h: Cross-platform path normalization
 * - atomic_file.h: TOCTOU-safe atomic file operations
 * - subagent_approval.h: Subagent approval proxy IPC
 */

/**
 * Gate actions determine how a category of tools is handled.
 */
typedef enum {
    GATE_ACTION_ALLOW,      /* Execute without prompting */
    GATE_ACTION_GATE,       /* Require user approval */
    GATE_ACTION_DENY        /* Never execute */
} GateAction;

/**
 * Tool categories for gate configuration.
 * Each category can be independently configured with a GateAction.
 */
typedef enum {
    GATE_CATEGORY_FILE_WRITE,   /* write_file, append_file, apply_delta */
    GATE_CATEGORY_FILE_READ,    /* read_file, file_info, list_dir, search_files */
    GATE_CATEGORY_SHELL,        /* shell command execution */
    GATE_CATEGORY_NETWORK,      /* web_fetch and network operations */
    GATE_CATEGORY_MEMORY,       /* remember, recall_memories, forget_memory, vector_db_*, todo */
    GATE_CATEGORY_SUBAGENT,     /* subagent, subagent_status */
    GATE_CATEGORY_MCP,          /* All mcp_* tools */
    GATE_CATEGORY_PYTHON,       /* python interpreter, dynamic tools */
    GATE_CATEGORY_COUNT         /* Sentinel for array sizing */
} GateCategory;

/**
 * Result of an approval check or user prompt.
 */
typedef enum {
    APPROVAL_ALLOWED,           /* User approved this single operation */
    APPROVAL_DENIED,            /* User denied the operation */
    APPROVAL_ALLOWED_ALWAYS,    /* User approved and added pattern to session allowlist */
    APPROVAL_ABORTED,           /* User pressed Ctrl+C, abort workflow */
    APPROVAL_RATE_LIMITED       /* Tool is in backoff period from previous denials */
} ApprovalResult;

/**
 * Shell types for cross-platform parsing.
 */
typedef enum {
    SHELL_TYPE_POSIX,       /* bash, sh, zsh, dash */
    SHELL_TYPE_CMD,         /* Windows cmd.exe */
    SHELL_TYPE_POWERSHELL,  /* PowerShell (Windows or Core) */
    SHELL_TYPE_UNKNOWN
} ShellType;

/**
 * Result codes from path verification operations.
 */
typedef enum {
    VERIFY_OK,                  /* Path verified successfully */
    VERIFY_ERR_SYMLINK,         /* Path is a symlink (O_NOFOLLOW) */
    VERIFY_ERR_DELETED,         /* File was deleted after approval */
    VERIFY_ERR_OPEN,            /* Failed to open file */
    VERIFY_ERR_STAT,            /* Failed to stat file */
    VERIFY_ERR_INODE_MISMATCH,  /* Inode/device changed since approval */
    VERIFY_ERR_PARENT,          /* Cannot open parent directory */
    VERIFY_ERR_PARENT_CHANGED,  /* Parent directory inode changed */
    VERIFY_ERR_ALREADY_EXISTS,  /* File exists when creating new file */
    VERIFY_ERR_CREATE           /* Failed to create new file */
} VerifyResult;

/**
 * Shell-specific allowlist entry.
 * Uses parsed command prefix matching rather than regex.
 */
typedef struct {
    char **command_prefix;  /* Command tokens, e.g., ["git", "status"] */
    int prefix_len;         /* Number of tokens in prefix */
    ShellType shell_type;   /* Optional: only match this shell type, or UNKNOWN for any */
} ShellAllowEntry;

/**
 * General allowlist entry for non-shell tools.
 * Uses regex pattern matching against the tool's match target.
 */
typedef struct {
    char *tool;             /* Tool name (exact match, no wildcards) */
    char *pattern;          /* POSIX extended regex pattern */
    regex_t compiled;       /* Pre-compiled regex for efficiency */
    int valid;              /* Whether regex compilation succeeded */
} AllowlistEntry;

/**
 * Denial tracking for rate limiting.
 * Implements exponential backoff on repeated denials.
 */
typedef struct {
    char *tool;             /* Tool name being tracked */
    GateCategory category;  /* Category of the tool */
    int denial_count;       /* Consecutive denials */
    time_t last_denial;     /* Timestamp of most recent denial */
    time_t backoff_until;   /* Don't prompt until after this time */
} DenialTracker;

/**
 * IPC channel for subagent approval proxying.
 * Parent maintains TTY ownership; subagents send requests via pipe.
 */
typedef struct ApprovalChannel {
    int request_fd;         /* Subagent writes requests here */
    int response_fd;        /* Parent writes responses here */
    pid_t subagent_pid;     /* PID of the subagent */
} ApprovalChannel;

/**
 * Approval request sent from subagent to parent.
 */
typedef struct {
    char *tool_name;        /* Name of the tool requiring approval */
    char *arguments_json;   /* Tool arguments as JSON string */
    char *display_summary;  /* Human-readable summary for prompt */
    uint32_t request_id;    /* Unique ID for request/response matching */
} ApprovalRequest;

/**
 * Approval response sent from parent to subagent.
 */
typedef struct {
    uint32_t request_id;    /* Matches the request */
    ApprovalResult result;  /* The user's decision */
    char *pattern;          /* If ALLOWED_ALWAYS, the generated pattern */
} ApprovalResponse;

/**
 * Approved path with TOCTOU protection data.
 * Captures filesystem state at approval time for verification at execution.
 */
typedef struct {
    char *user_path;        /* Original path from tool call */
    char *resolved_path;    /* Canonical path at approval time */

    /* For existing files */
    ino_t inode;            /* Inode at approval (0 if new file) */
    dev_t device;           /* Device at approval */

    /* For new files - parent directory verification */
    ino_t parent_inode;     /* Parent directory inode */
    dev_t parent_device;    /* Parent directory device */
    char *parent_path;      /* Resolved parent path */

    int existed;            /* File existed at approval time */
    int is_network_fs;      /* Detected as NFS/CIFS/etc */

#ifdef _WIN32
    DWORD volume_serial;    /* Windows volume serial number */
    DWORD index_high;       /* Windows file index (high) */
    DWORD index_low;        /* Windows file index (low) */
#endif
} ApprovedPath;

/**
 * Main approval gate configuration.
 * Holds all gate settings, allowlists, and runtime state.
 */
typedef struct {
    int enabled;            /* Master switch for approval gates */

    /* Category configuration */
    GateAction categories[GATE_CATEGORY_COUNT];

    /* General allowlist (regex-based) */
    AllowlistEntry *allowlist;
    int allowlist_count;
    int allowlist_capacity;

    /* Shell-specific allowlist (command prefix matching) */
    ShellAllowEntry *shell_allowlist;
    int shell_allowlist_count;
    int shell_allowlist_capacity;

    /* Denial rate limiting */
    DenialTracker *denial_trackers;
    int denial_tracker_count;
    int denial_tracker_capacity;

    /* Subagent approval channel (NULL for root process) */
    ApprovalChannel *approval_channel;
} ApprovalGateConfig;

/* ============================================================================
 * Initialization and Cleanup
 * ========================================================================== */

/**
 * Initialize gate configuration with defaults.
 * Loads settings from config file if available.
 *
 * @param config Configuration structure to initialize
 * @return 0 on success, -1 on failure
 */
int approval_gate_init(ApprovalGateConfig *config);

/**
 * Initialize child config from parent (for subagents).
 * Inherits category config and static allowlist, NOT session allowlist.
 *
 * @param child Child configuration to initialize
 * @param parent Parent configuration to inherit from
 * @return 0 on success, -1 on failure
 */
int approval_gate_init_from_parent(ApprovalGateConfig *child,
                                   const ApprovalGateConfig *parent);

/**
 * Free all resources held by gate configuration.
 *
 * @param config Configuration to clean up
 */
void approval_gate_cleanup(ApprovalGateConfig *config);

/* ============================================================================
 * Category and Tool Mapping
 * ========================================================================== */

/**
 * Map a tool name to its category.
 *
 * @param tool_name Name of the tool
 * @return The tool's category
 */
GateCategory get_tool_category(const char *tool_name);

/**
 * Get the configured action for a category.
 *
 * @param config Gate configuration
 * @param category The category to check
 * @return The configured action (ALLOW, GATE, or DENY)
 */
GateAction approval_gate_get_category_action(const ApprovalGateConfig *config,
                                             GateCategory category);

/* ============================================================================
 * Approval Checking
 * ========================================================================== */

/**
 * Check if a tool call requires approval (doesn't prompt).
 *
 * @param config Gate configuration
 * @param tool_call The tool call to check
 * @return 1 if approval required, 0 if allowed, -1 if denied
 */
int approval_gate_requires_check(const ApprovalGateConfig *config,
                                 const ToolCall *tool_call);

/**
 * Prompt user for approval (TTY only).
 * Displays formatted prompt and reads single keypress.
 *
 * @param config Gate configuration (may be modified if ALLOWED_ALWAYS)
 * @param tool_call The tool call requiring approval
 * @param out_path Output: path verification data for file operations
 * @return User's decision
 */
ApprovalResult approval_gate_prompt(ApprovalGateConfig *config,
                                    const ToolCall *tool_call,
                                    ApprovedPath *out_path);

/**
 * Combined check and prompt.
 * First checks if approval is needed, then prompts if required.
 *
 * @param config Gate configuration
 * @param tool_call The tool call to check
 * @param out_path Output: path verification data for file operations
 * @return Result of the check/prompt
 */
ApprovalResult check_approval_gate(ApprovalGateConfig *config,
                                   const ToolCall *tool_call,
                                   ApprovedPath *out_path);

/* ============================================================================
 * Allowlist Management
 * ========================================================================== */

/**
 * Add a regex pattern to the session allowlist.
 *
 * @param config Gate configuration
 * @param tool Tool name
 * @param pattern POSIX extended regex pattern
 * @return 0 on success, -1 on failure
 */
int approval_gate_add_allowlist(ApprovalGateConfig *config,
                                const char *tool,
                                const char *pattern);

/**
 * Add a shell command prefix to the session allowlist.
 *
 * @param config Gate configuration
 * @param command_prefix Array of command tokens
 * @param prefix_len Number of tokens
 * @param shell_type Shell type (SHELL_TYPE_UNKNOWN for any)
 * @return 0 on success, -1 on failure
 */
int approval_gate_add_shell_allowlist(ApprovalGateConfig *config,
                                      const char **command_prefix,
                                      int prefix_len,
                                      ShellType shell_type);

/**
 * Check if a tool call matches any allowlist entry.
 *
 * @param config Gate configuration
 * @param tool_call The tool call to check
 * @return 1 if matched (allowed), 0 if not matched
 */
int approval_gate_matches_allowlist(const ApprovalGateConfig *config,
                                    const ToolCall *tool_call);

/* ============================================================================
 * Rate Limiting
 * ========================================================================== */

/**
 * Check if a tool is rate-limited from previous denials.
 *
 * @param config Gate configuration
 * @param tool_call The tool call to check
 * @return 1 if rate limited, 0 if not
 */
int is_rate_limited(const ApprovalGateConfig *config,
                    const ToolCall *tool_call);

/**
 * Record a denial for rate limiting.
 *
 * @param config Gate configuration
 * @param tool_call The denied tool call
 */
void track_denial(ApprovalGateConfig *config,
                  const ToolCall *tool_call);

/**
 * Reset denial counter for a tool (on approval or backoff expiry).
 *
 * @param config Gate configuration
 * @param tool Tool name to reset
 */
void reset_denial_tracker(ApprovalGateConfig *config,
                          const char *tool);

/**
 * Get the remaining backoff time for a rate-limited tool.
 *
 * @param config Gate configuration
 * @param tool Tool name
 * @return Seconds remaining, or 0 if not rate limited
 */
int get_rate_limit_remaining(const ApprovalGateConfig *config,
                             const char *tool);

/* ============================================================================
 * Path Verification (TOCTOU Protection)
 * ========================================================================== */

/**
 * Verify that an approved path hasn't changed since approval.
 *
 * @param approved The approved path data
 * @return VERIFY_OK on success, or specific error code
 */
VerifyResult verify_approved_path(const ApprovedPath *approved);

/**
 * Verify and open an approved path atomically.
 * Opens file with O_NOFOLLOW and verifies inode matches approval.
 *
 * @param approved The approved path data
 * @param flags Open flags (O_RDONLY, O_WRONLY, etc.)
 * @param out_fd Output: file descriptor on success
 * @return VERIFY_OK on success, or specific error code
 */
VerifyResult verify_and_open_approved_path(const ApprovedPath *approved,
                                           int flags,
                                           int *out_fd);

/**
 * Free resources held by an ApprovedPath.
 *
 * @param path The path to free
 */
void free_approved_path(ApprovedPath *path);

/* ============================================================================
 * Subagent Approval Proxy
 * ========================================================================== */

/**
 * Request approval from parent process (subagent side).
 *
 * @param channel IPC channel to parent
 * @param tool_call The tool call requiring approval
 * @param out_path Output: path verification data
 * @return Parent's decision (DENIED on timeout)
 */
ApprovalResult subagent_request_approval(const ApprovalChannel *channel,
                                         const ToolCall *tool_call,
                                         ApprovedPath *out_path);

/**
 * Handle approval request from subagent (parent side).
 *
 * @param config Parent's gate configuration
 * @param channel IPC channel to subagent
 */
void handle_subagent_approval_request(ApprovalGateConfig *config,
                                      ApprovalChannel *channel);

/**
 * Free resources held by an ApprovalChannel.
 *
 * @param channel Channel to clean up
 */
void free_approval_channel(ApprovalChannel *channel);

/* ============================================================================
 * Shell Detection
 * ========================================================================== */

/**
 * Detect the shell type from environment.
 * On Windows: checks COMSPEC and PSModulePath.
 * On POSIX: checks SHELL for pwsh/powershell, defaults to POSIX.
 *
 * @return Detected shell type
 */
ShellType detect_shell_type(void);

/* ============================================================================
 * Error Formatting
 * ========================================================================== */

/**
 * Format a rate limit error message as JSON.
 *
 * @param config Gate configuration
 * @param tool_call The rate-limited tool call
 * @return Allocated JSON error string. Caller must free.
 */
char *format_rate_limit_error(const ApprovalGateConfig *config,
                              const ToolCall *tool_call);

/**
 * Format a denial error message as JSON.
 *
 * @param tool_call The denied tool call
 * @return Allocated JSON error string. Caller must free.
 */
char *format_denial_error(const ToolCall *tool_call);

/**
 * Format a protected file error message as JSON.
 *
 * @param path The protected file path
 * @return Allocated JSON error string. Caller must free.
 */
char *format_protected_file_error(const char *path);

/**
 * Format a path verification error message as JSON.
 *
 * @param result The verification result
 * @param path The path that failed verification
 * @return Allocated JSON error string. Caller must free.
 */
char *format_verify_error(VerifyResult result, const char *path);

/* ============================================================================
 * Utility Functions
 * ========================================================================== */

/**
 * Get category name as string.
 *
 * @param category The category
 * @return Static string name
 */
const char *gate_category_name(GateCategory category);

/**
 * Get action name as string.
 *
 * @param action The action
 * @return Static string name
 */
const char *gate_action_name(GateAction action);

/**
 * Get approval result name as string.
 *
 * @param result The result
 * @return Static string name
 */
const char *approval_result_name(ApprovalResult result);

/**
 * Get verify result error message.
 *
 * @param result The verification result
 * @return Static string message
 */
const char *verify_result_message(VerifyResult result);

/* ============================================================================
 * CLI Override Functions
 * ========================================================================== */

/**
 * Enable yolo mode (disable all gates).
 * This is typically called after approval_gate_init() to apply the --yolo flag.
 *
 * @param config Gate configuration
 */
void approval_gate_enable_yolo(ApprovalGateConfig *config);

/**
 * Override a category's action from CLI.
 * CLI flags take precedence over config file settings.
 *
 * @param config Gate configuration
 * @param category_name Category name string (e.g., "file_write", "shell")
 * @param action The action to set
 * @return 0 on success, -1 if category name is invalid
 */
int approval_gate_set_category_action(ApprovalGateConfig *config,
                                      const char *category_name,
                                      GateAction action);

/**
 * Parse and add a CLI --allow entry.
 * Format: "tool:arg1,arg2,..." for shell commands
 * Format: "tool:pattern" for regex patterns
 *
 * For shell commands (tool="shell"), the arguments are parsed as command prefix.
 * For other tools, the argument is treated as a regex pattern.
 *
 * @param config Gate configuration
 * @param allow_spec The allow specification string (e.g., "shell:git,status")
 * @return 0 on success, -1 on failure
 */
int approval_gate_add_cli_allow(ApprovalGateConfig *config,
                                const char *allow_spec);

/**
 * Parse category name from string.
 *
 * @param name Category name string
 * @param out_category Output: the parsed category
 * @return 0 on success, -1 if name is not recognized
 */
int approval_gate_parse_category(const char *name, GateCategory *out_category);

#endif /* APPROVAL_GATE_H */
