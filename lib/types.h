#ifndef LIB_TYPES_H
#define LIB_TYPES_H

#include <stddef.h>

typedef struct ToolCall {
    char *id;           /* Tool call ID from the model */
    char *name;         /* Function name to call */
    char *arguments;    /* JSON string of arguments */
} ToolCall;

typedef struct ToolResult {
    char *tool_call_id; /* Matching the tool call ID */
    char *result;       /* Tool execution result as string */
    int success;        /* 1 if successful, 0 if error */
    int clear_history;  /* Signal session to clear conversation after this result */
} ToolResult;

typedef struct StreamingToolUse {
    char* id;
    char* name;
    char* arguments_json;
    size_t arguments_capacity;
} StreamingToolUse;

#endif /* LIB_TYPES_H */
