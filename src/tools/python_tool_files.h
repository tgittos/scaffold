#ifndef PYTHON_TOOL_FILES_H
#define PYTHON_TOOL_FILES_H

#include "tools_system.h"

/**
 * Directory name where Python tool files are stored (relative to ralph home)
 */
#define PYTHON_TOOLS_DIR_NAME "tools"

/**
 * Maximum number of Python tools that can be loaded
 */
#define MAX_PYTHON_TOOLS 32

/**
 * Structure representing a loaded Python tool
 */
typedef struct {
    char *name;              // Tool name (derived from function name)
    char *description;       // Tool description (from docstring)
    char *file_path;         // Path to the .py file
    int parameter_count;     // Number of parameters
    ToolParameter *parameters; // Parameter definitions
} PythonToolDef;

/**
 * Structure holding all loaded Python tools
 */
typedef struct {
    PythonToolDef *tools;
    int count;
    char *tools_dir;        // Path to ~/.local/ralph/tools/
} PythonToolRegistry;

/**
 * Initialize Python tool files system.
 * Creates ~/.local/ralph/tools/ if it doesn't exist and
 * extracts default tools from embedded /zip/python_defaults/ if needed.
 *
 * @return 0 on success, -1 on failure
 */
int python_init_tool_files(void);

/**
 * Load all Python tool files from ~/.local/ralph/tools/ into the
 * Python interpreter's global scope.
 *
 * Must be called after python_interpreter_init().
 *
 * @return 0 on success, -1 on failure
 */
int python_load_tool_files(void);

/**
 * Register all loaded Python tools with the tool registry.
 * Extracts function signatures and docstrings to generate tool schemas.
 *
 * @param registry Tool registry to register with
 * @return 0 on success, -1 on failure
 */
int python_register_tool_schemas(ToolRegistry *registry);

/**
 * Execute a Python tool call.
 * Routes the tool call to the appropriate Python function.
 *
 * @param tool_call Tool call to execute
 * @param result Result structure to fill
 * @return 0 on success, -1 on failure
 */
int execute_python_file_tool_call(const ToolCall *tool_call, ToolResult *result);

/**
 * Check if a tool name corresponds to a loaded Python tool.
 *
 * @param name Tool name to check
 * @return 1 if it's a Python tool, 0 otherwise
 */
int is_python_file_tool(const char *name);

/**
 * Get the path to the Python tools directory.
 *
 * @return Path to ~/.local/ralph/tools/ or NULL if not initialized
 */
const char* python_get_tools_dir(void);

/**
 * Get a description of all loaded Python tools for the system prompt.
 *
 * @return Dynamically allocated string listing tools, caller must free
 */
char* python_get_loaded_tools_description(void);

/**
 * Shutdown and cleanup Python tool files system.
 */
void python_cleanup_tool_files(void);

/**
 * Reset Python tools to defaults by re-extracting from embedded files.
 * User-modified tools will be backed up with .bak extension.
 *
 * @return 0 on success, -1 on failure
 */
int python_reset_tool_files(void);

#endif // PYTHON_TOOL_FILES_H
