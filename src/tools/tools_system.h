#ifndef TOOLS_SYSTEM_H
#define TOOLS_SYSTEM_H

#include <stddef.h>
#include "util/darray.h"

/**
 * Structure representing a tool parameter
 */
typedef struct {
    char *name;
    char *type;          // "string", "number", "boolean", "object", "array"
    char *description;
    char **enum_values;  // For enum types, NULL if not enum
    int enum_count;
    int required;        // 1 if required, 0 if optional
    char *items_schema;  // For array types: JSON schema defining item structure, NULL for default {"type": "object"}
} ToolParameter;

/**
 * Structure representing a tool call from the model
 */
typedef struct {
    char *id;           // Tool call ID from the model
    char *name;         // Function name to call
    char *arguments;    // JSON string of arguments
} ToolCall;

/**
 * Structure representing tool call result
 */
typedef struct {
    char *tool_call_id; // Matching the tool call ID
    char *result;       // Tool execution result as string
    int success;        // 1 if successful, 0 if error
} ToolResult;

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
} ToolFunction;

DARRAY_DECLARE(ToolFunctionArray, ToolFunction)

/**
 * Structure containing all available tools
 */
typedef struct {
    ToolFunctionArray functions;
} ToolRegistry;

/**
 * Initialize an empty tool registry
 *
 * @param registry Pointer to ToolRegistry structure to initialize
 */
void init_tool_registry(ToolRegistry *registry);

/**
 * Register all built-in tools that are compiled into the binary
 *
 * @param registry Pointer to ToolRegistry structure to populate
 * @return 0 on success, -1 on failure
 */
int register_builtin_tools(ToolRegistry *registry);

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
 * Generate JSON tools array for API request (OpenAI format)
 *
 * @param registry Pointer to ToolRegistry structure
 * @return Dynamically allocated JSON string, caller must free
 */
char* generate_tools_json(const ToolRegistry *registry);

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
int execute_tool_call(const ToolRegistry *registry, const ToolCall *tool_call, ToolResult *result);

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
