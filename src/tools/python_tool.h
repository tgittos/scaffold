#ifndef PYTHON_TOOL_H
#define PYTHON_TOOL_H

#include "tools_system.h"

/**
 * Maximum sizes for Python tool operations
 */
#define PYTHON_MAX_CODE_SIZE (1024 * 1024)   // 1MB max code size
#define PYTHON_DEFAULT_TIMEOUT 30             // Default timeout in seconds
#define PYTHON_MAX_OUTPUT_SIZE (512 * 1024)  // 512KB max output size

/**
 * Structure representing Python code execution parameters
 */
typedef struct {
    char *code;              // Python code to execute
    int timeout_seconds;     // Timeout in seconds (0 for default)
    int capture_stderr;      // 1 to capture stderr separately, 0 to merge
} PythonExecutionParams;

/**
 * Structure representing Python code execution result
 */
typedef struct {
    char *stdout_output;     // Standard output from print() statements
    char *stderr_output;     // Standard error output
    char *exception;         // Exception message if code raised an error, NULL otherwise
    int success;             // 1 if execution succeeded, 0 if error
    double execution_time;   // Execution time in seconds
    int timed_out;           // 1 if execution timed out, 0 otherwise
} PythonExecutionResult;

/**
 * Initialize the Python interpreter
 *
 * Sets up PYTHONHOME for embedded stdlib and initializes the interpreter.
 * Should be called once at ralph startup before any Python tool calls.
 *
 * @return 0 on success, -1 on failure
 */
int python_interpreter_init(void);

/**
 * Shutdown the Python interpreter
 *
 * Finalizes the Python interpreter and releases resources.
 * Should be called once at ralph shutdown.
 */
void python_interpreter_shutdown(void);

/**
 * Register the Python execution tool with the tool registry
 *
 * @param registry Pointer to ToolRegistry structure
 * @return 0 on success, -1 on failure
 */
int register_python_tool(ToolRegistry *registry);

/**
 * Execute a Python tool call and format result for tool system
 *
 * @param tool_call Tool call structure from tools system
 * @param result Tool result structure for tools system
 * @return 0 on success, -1 on failure
 */
int execute_python_tool_call(const ToolCall *tool_call, ToolResult *result);

/**
 * Parse Python tool arguments from JSON string
 *
 * @param json_args JSON string containing arguments
 * @param params Output parameters structure (caller must free using cleanup_python_params)
 * @return 0 on success, -1 on failure
 */
int parse_python_arguments(const char *json_args, PythonExecutionParams *params);

/**
 * Execute Python code with the persistent interpreter
 *
 * @param params Python execution parameters
 * @param result Output execution result (caller must free using cleanup_python_result)
 * @return 0 on success, -1 on failure
 */
int execute_python_code(const PythonExecutionParams *params, PythonExecutionResult *result);

/**
 * Format Python execution result as JSON string
 *
 * @param exec_result Python execution result
 * @return Dynamically allocated JSON string, caller must free
 */
char* format_python_result_json(const PythonExecutionResult *exec_result);

/**
 * Clean up memory allocated for Python execution parameters
 *
 * @param params Python execution parameters to cleanup
 */
void cleanup_python_params(PythonExecutionParams *params);

/**
 * Clean up memory allocated for Python execution result
 *
 * @param result Python execution result to cleanup
 */
void cleanup_python_result(PythonExecutionResult *result);

/**
 * Check if the Python interpreter has been initialized
 *
 * @return 1 if initialized, 0 if not
 */
int python_interpreter_is_initialized(void);

#endif // PYTHON_TOOL_H
