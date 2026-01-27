#ifndef TOOL_RESULT_BUILDER_H
#define TOOL_RESULT_BUILDER_H

#include "tools_system.h"

/**
 * Tool Result Builder - Standardized tool result creation
 * 
 * This provides a consistent way to create tool results with proper
 * memory management and error handling patterns.
 */

typedef struct tool_result_builder tool_result_builder_t;

/**
 * Create a new tool result builder
 * 
 * @param tool_call_id Tool call ID to associate with result
 * @return New builder instance or NULL on failure
 */
tool_result_builder_t* tool_result_builder_create(const char* tool_call_id);

/**
 * Set success result with formatted message
 * 
 * @param builder Builder instance
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return 0 on success, -1 on failure
 */
int tool_result_builder_set_success(tool_result_builder_t* builder, const char* format, ...);

/**
 * Set error result with formatted message
 * 
 * @param builder Builder instance
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return 0 on success, -1 on failure
 */
int tool_result_builder_set_error(tool_result_builder_t* builder, const char* format, ...);

/**
 * Set success result with JSON object
 * 
 * @param builder Builder instance
 * @param json_object JSON object string
 * @return 0 on success, -1 on failure
 */
int tool_result_builder_set_success_json(tool_result_builder_t* builder, const char* json_object);

/**
 * Set error result with JSON object
 * 
 * @param builder Builder instance
 * @param error_message Error message
 * @return 0 on success, -1 on failure
 */
int tool_result_builder_set_error_json(tool_result_builder_t* builder, const char* error_message);

/**
 * Finalize and get the tool result
 * Builder is destroyed after this call
 * 
 * @param builder Builder instance (will be freed)
 * @return Complete ToolResult or NULL on failure
 */
ToolResult* tool_result_builder_finalize(tool_result_builder_t* builder);

/**
 * Destroy builder without creating result (cleanup)
 *
 * @param builder Builder instance to destroy
 */
void tool_result_builder_destroy(tool_result_builder_t* builder);

#endif // TOOL_RESULT_BUILDER_H