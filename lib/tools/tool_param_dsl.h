/*
 * tool_param_dsl.h - Table-driven tool parameter registration
 *
 * Provides a declarative way to define tool parameters using static tables,
 * eliminating hundreds of lines of repetitive registration boilerplate.
 */

#ifndef TOOL_PARAM_DSL_H
#define TOOL_PARAM_DSL_H

#include "tools_system.h"

/*
 * ParamDef - Static parameter definition
 *
 * All strings are const and should point to string literals.
 * Memory management is handled internally during registration.
 */
typedef struct {
    const char *name;        /* Parameter name */
    const char *type;        /* Type: "string", "number", "array", "object", "boolean" */
    const char *description; /* Human-readable description */
    const char **enum_values; /* Optional enum values (NULL-terminated array or NULL) */
    int required;            /* 1 if required, 0 if optional */
} ParamDef;

/*
 * ToolDef - Static tool definition
 *
 * Combines tool metadata with parameter definitions and execute function.
 */
typedef struct {
    const char *name;              /* Tool name */
    const char *description;       /* Tool description */
    const ParamDef *params;        /* Array of parameter definitions (may be NULL) */
    int param_count;               /* Number of parameters */
    tool_execute_func_t execute;   /* Execute function pointer */
} ToolDef;

/*
 * Register a single tool from a ToolDef.
 *
 * @param registry  The tool registry
 * @param def       Tool definition
 * @return          0 on success, -1 on failure
 */
int register_tool_from_def(ToolRegistry *registry, const ToolDef *def);

/*
 * Register multiple tools from an array of ToolDefs.
 *
 * Stops on first failure and returns the number of successfully registered tools.
 *
 * @param registry  The tool registry
 * @param defs      Array of tool definitions
 * @param count     Number of tools in the array
 * @return          Number of tools successfully registered (count on full success)
 */
int register_tools_from_defs(ToolRegistry *registry, const ToolDef *defs, int count);

/*
 * Count enum values in a NULL-terminated array.
 *
 * @param enum_values  NULL-terminated array of strings, or NULL
 * @return             Number of enum values (0 if NULL)
 */
int count_enum_values(const char **enum_values);

/*
 * OperationDispatchEntry - Maps an operation name to a handler function.
 * Used with dispatch_by_operation for table-driven dispatch.
 */
typedef struct {
    const char *name;
    tool_execute_func_t handler;
} OperationDispatchEntry;

/*
 * Dispatch a tool call by its "operation" parameter.
 *
 * Extracts the "operation" string from tc->arguments, looks it up in the
 * dispatch table, and calls the matching handler. On missing or unknown
 * operation, sets an error result with details.
 *
 * @param tc       The tool call
 * @param result   Output result
 * @param table    Array of {name, handler} entries
 * @param count    Number of entries in the table
 * @return         Handler return code, or 0 on dispatch error
 */
int dispatch_by_operation(const ToolCall *tc, ToolResult *result,
                          const OperationDispatchEntry *table, int count);

#endif /* TOOL_PARAM_DSL_H */
