#ifndef TOOLS_SYSTEM_H
#define TOOLS_SYSTEM_H

#include <stddef.h>
#include "../util/darray.h"
#include "../types.h"

typedef struct Services Services;
typedef struct ToolCache ToolCache;

/**
 * Structure representing a tool parameter
 */
typedef struct {
    char *name;
    char *type;          /* "string", "number", "boolean", "object", "array" */
    char *description;
    char **enum_values;  /* For enum types, NULL if not enum */
    int enum_count;
    int required;        /* 1 if required, 0 if optional */
    char *items_schema;  /* For array types: JSON schema defining item structure, NULL for default {"type": "object"} */
} ToolParameter;

// Forward declaration for the execution function type
typedef int (*tool_execute_func_t)(const ToolCall *tool_call, ToolResult *result);

/**
 * Structure representing a tool function definition
 */
typedef struct {
    char *name;
    char *description;
    ToolParameter *parameters;
    int parameter_count;
    tool_execute_func_t execute_func;  // Function pointer for tool execution
    int cacheable;                     // 1 if results can be cached, 0 otherwise
    int thread_safe;                   // 1 if safe for concurrent execution, 0 otherwise
} ToolFunction;

DARRAY_DECLARE(ToolFunctionArray, ToolFunction)

/**
 * Structure containing all available tools
 */
typedef struct ToolRegistry {
    ToolFunctionArray functions;
    Services* services;
    ToolCache *cache;
} ToolRegistry;

/**
 * Initialize an empty tool registry
 *
 * @param registry Pointer to ToolRegistry structure to initialize
 */
void init_tool_registry(ToolRegistry *registry);

/**
 * Register a tool with the registry
 *
 * @param registry Pointer to ToolRegistry structure
 * @param name Tool name
 * @param description Tool description
 * @param parameters Array of tool parameters
 * @param param_count Number of parameters
 * @param execute_func Function pointer for tool execution
 * @return 0 on success, -1 on failure
 */
int register_tool(ToolRegistry *registry, const char *name, const char *description,
                  ToolParameter *parameters, int param_count, tool_execute_func_t execute_func);

/**
 * Mark a tool as cacheable by name
 *
 * @param registry Pointer to ToolRegistry structure
 * @param tool_name Name of the tool to mark cacheable
 * @param cacheable 1 to enable caching, 0 to disable
 * @return 0 on success, -1 if tool not found
 */
int tool_set_cacheable(ToolRegistry *registry, const char *tool_name, int cacheable);

/**
 * Mark a tool as thread-safe for parallel batch execution
 *
 * Only tools whose execute_func uses no unprotected global state should
 * be marked thread_safe. When a batch contains any non-thread-safe tool,
 * the entire batch falls back to serial execution.
 *
 * @param registry Pointer to ToolRegistry structure
 * @param tool_name Name of the tool to mark
 * @param thread_safe 1 to allow parallel execution, 0 to require serial
 * @return 0 on success, -1 if tool not found
 */
int tool_set_thread_safe(ToolRegistry *registry, const char *tool_name, int thread_safe);

/**
 * Generate JSON tools array for API request (OpenAI format)
 *
 * @param registry Pointer to ToolRegistry structure
 * @return Dynamically allocated JSON string, caller must free
 */
char* generate_tools_json(const ToolRegistry *registry);

/**
 * Generate JSON tools array in flat format for Codex/Responses API.
 * Produces {type, name, description, parameters} directly.
 *
 * @param registry Pointer to ToolRegistry structure
 * @return Dynamically allocated JSON string, caller must free
 */
char* generate_tools_json_flat(const ToolRegistry *registry);

/**
 * Generate JSON tools array for Anthropic API request
 *
 * @param registry Pointer to ToolRegistry structure
 * @return Dynamically allocated JSON string, caller must free
 */
char* generate_anthropic_tools_json(const ToolRegistry *registry);

/**
 * Parse tool calls from API response JSON
 *
 * @param json_response Raw JSON response from API
 * @param tool_calls Output array of tool calls, caller must free
 * @param call_count Output number of tool calls found
 * @return 0 on success, -1 on failure
 */
int parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count);

/**
 * Parse tool calls from Anthropic API response JSON
 *
 * @param json_response Raw JSON response from Anthropic API
 * @param tool_calls Output array of tool calls, caller must free
 * @param call_count Output number of tool calls found
 * @return 0 on success, -1 on failure
 */
int parse_anthropic_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count);

/**
 * Execute a tool call and return result
 *
 * @param registry Pointer to ToolRegistry structure
 * @param tool_call Tool call to execute
 * @param result Output tool result, caller must free result->result
 * @return 0 on success, -1 on failure
 */
int execute_tool_call(ToolRegistry *registry, const ToolCall *tool_call, ToolResult *result);

/**
 * Generate JSON message for tool results to send back to model
 *
 * @param results Array of tool results
 * @param result_count Number of results
 * @return Dynamically allocated JSON string, caller must free
 */
char* generate_tool_results_json(const ToolResult *results, int result_count);

/**
 * Generate a single tool result message for conversation history
 *
 * @param result Tool result to format as message
 * @return Dynamically allocated message string, caller must free
 */
char* generate_single_tool_message(const ToolResult *result);

/**
 * Clean up memory allocated for tool registry
 *
 * @param registry Pointer to ToolRegistry structure to cleanup
 */
void cleanup_tool_registry(ToolRegistry *registry);

/**
 * Clean up memory allocated for tool calls
 *
 * @param tool_calls Array of tool calls to cleanup
 * @param call_count Number of tool calls
 */
void cleanup_tool_calls(ToolCall *tool_calls, int call_count);

/**
 * Clean up memory allocated for tool results
 *
 * @param results Array of tool results to cleanup
 * @param result_count Number of results
 */
void cleanup_tool_results(ToolResult *results, int result_count);

#endif // TOOLS_SYSTEM_H
