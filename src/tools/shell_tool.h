#ifndef SHELL_TOOL_H
#define SHELL_TOOL_H

#include "tools_system.h"
#include <sys/types.h>
#include <time.h>

/**
 * Maximum sizes for shell tool operations
 */
#define SHELL_MAX_COMMAND_LENGTH 4096
#define SHELL_MAX_OUTPUT_LENGTH 65536
#define SHELL_MAX_TIMEOUT_SECONDS 300

/**
 * Structure representing shell command execution result
 */
typedef struct {
    char *stdout_output;     // Standard output from command
    char *stderr_output;     // Standard error from command
    int exit_code;           // Exit code from command
    double execution_time;   // Execution time in seconds
    int timed_out;           // 1 if command timed out, 0 otherwise
} ShellExecutionResult;

/**
 * Structure representing shell command parameters
 */
typedef struct {
    char *command;           // Shell command to execute
    char *working_directory; // Working directory (optional)
    int timeout_seconds;     // Timeout in seconds (0 for no timeout)
    char **environment;      // Environment variables (NULL-terminated array)
    int capture_stderr;      // 1 to capture stderr separately, 0 to merge with stdout
} ShellCommandParams;

/**
 * Register the shell execution tool with the tool registry
 * 
 * @param registry Pointer to ToolRegistry structure
 * @return 0 on success, -1 on failure
 */
int register_shell_tool(ToolRegistry *registry);

/**
 * Execute a shell command with proper error handling and timeout support
 * 
 * @param params Shell command parameters
 * @param result Output execution result (caller must free using cleanup_shell_result)
 * @return 0 on success, -1 on failure
 */
int execute_shell_command(const ShellCommandParams *params, ShellExecutionResult *result);

/**
 * Parse shell tool arguments from JSON string
 * 
 * @param json_args JSON string containing arguments
 * @param params Output parameters structure (caller must free using cleanup_shell_params)
 * @return 0 on success, -1 on failure
 */
int parse_shell_arguments(const char *json_args, ShellCommandParams *params);

/**
 * Execute shell tool call and format result for tool system
 * 
 * @param tool_call Tool call structure from tools system
 * @param result Tool result structure for tools system
 * @return 0 on success, -1 on failure
 */
int execute_shell_tool_call(const ToolCall *tool_call, ToolResult *result);

/**
 * Format shell execution result as JSON string
 * 
 * @param exec_result Shell execution result
 * @return Dynamically allocated JSON string, caller must free
 */
char* format_shell_result_json(const ShellExecutionResult *exec_result);

/**
 * Validate shell command for safety (basic security checks)
 * 
 * @param command Command to validate
 * @return 1 if command appears safe, 0 if potentially dangerous
 */
int validate_shell_command(const char *command);

/**
 * Clean up memory allocated for shell command parameters
 * 
 * @param params Shell command parameters to cleanup
 */
void cleanup_shell_params(ShellCommandParams *params);

/**
 * Clean up memory allocated for shell execution result
 * 
 * @param result Shell execution result to cleanup
 */
void cleanup_shell_result(ShellExecutionResult *result);

#endif // SHELL_TOOL_H