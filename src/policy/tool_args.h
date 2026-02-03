#ifndef TOOL_ARGS_H
#define TOOL_ARGS_H

/**
 * Tool Arguments Module
 *
 * Centralized cJSON argument extraction from ToolCall structures.
 * All functions return newly allocated strings that must be freed by the caller.
 *
 * This module eliminates duplicate JSON parsing across approval_gate.c,
 * pattern_generator.c, and other policy modules.
 */

#include "lib/tools/tools_system.h"  /* For ToolCall */

/**
 * Get a string argument by key from a tool call's arguments JSON.
 *
 * @param tool_call The tool call
 * @param key The JSON key to extract
 * @return Allocated string value, or NULL if not found. Caller must free.
 */
char *tool_args_get_string(const ToolCall *tool_call, const char *key);

/**
 * Get the "command" argument from a shell tool call.
 *
 * @param tool_call The tool call
 * @return Allocated command string, or NULL if not found. Caller must free.
 */
char *tool_args_get_command(const ToolCall *tool_call);

/**
 * Get the file path from a tool call's arguments.
 * Tries common path argument names: "path", "file_path", "filepath", "filename"
 *
 * @param tool_call The tool call
 * @return Allocated path string, or NULL if not found. Caller must free.
 */
char *tool_args_get_path(const ToolCall *tool_call);

/**
 * Get the "url" argument from a network tool call.
 *
 * @param tool_call The tool call
 * @return Allocated URL string, or NULL if not found. Caller must free.
 */
char *tool_args_get_url(const ToolCall *tool_call);

/**
 * Get an integer argument by key from a tool call's arguments JSON.
 *
 * @param tool_call The tool call
 * @param key The JSON key to extract
 * @param out_value Output: the integer value
 * @return 0 on success, -1 if not found or not an integer
 */
int tool_args_get_int(const ToolCall *tool_call, const char *key, int *out_value);

/**
 * Get a boolean argument by key from a tool call's arguments JSON.
 *
 * @param tool_call The tool call
 * @param key The JSON key to extract
 * @param out_value Output: the boolean value (0 or 1)
 * @return 0 on success, -1 if not found or not a boolean
 */
int tool_args_get_bool(const ToolCall *tool_call, const char *key, int *out_value);

#endif /* TOOL_ARGS_H */
