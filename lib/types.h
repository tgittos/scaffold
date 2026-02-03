/**
 * lib/types.h - Shared types for the library
 *
 * This header defines types that are shared across multiple modules:
 * - ToolCall: Represents a tool call from the model
 * - ToolResult: Represents the result of a tool execution
 * - StreamingToolUse: Represents a tool call during streaming
 */

#ifndef LIB_TYPES_H
#define LIB_TYPES_H

#include <stddef.h>

/**
 * Structure representing a tool call from the model
 */
typedef struct ToolCall {
    char *id;           /* Tool call ID from the model */
    char *name;         /* Function name to call */
    char *arguments;    /* JSON string of arguments */
} ToolCall;

/**
 * Structure representing tool call result
 */
typedef struct ToolResult {
    char *tool_call_id; /* Matching the tool call ID */
    char *result;       /* Tool execution result as string */
    int success;        /* 1 if successful, 0 if error */
} ToolResult;

/**
 * Structure representing a tool call during streaming
 */
typedef struct StreamingToolUse {
    char* id;
    char* name;
    char* arguments_json;
    size_t arguments_capacity;
} StreamingToolUse;

#endif /* LIB_TYPES_H */
