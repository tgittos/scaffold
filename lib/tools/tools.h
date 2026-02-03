/**
 * lib/tools/tools.h - Library wrapper for tool system
 *
 * This header provides the public API for the tool system infrastructure.
 * The tool system provides:
 * - ToolRegistry: Container for available tools
 * - Tool registration and execution
 * - JSON generation for LLM API requests
 * - Tool call parsing from LLM responses
 *
 * Source implementation: lib/tools/tools_system.c
 */

#ifndef LIB_TOOLS_TOOLS_H
#define LIB_TOOLS_TOOLS_H

#include <stdlib.h>

/* Include the local implementation */
#include "tools_system.h"

/* Forward declaration - register_builtin_tools is in src/tools/builtin_tools.h */
int register_builtin_tools(ToolRegistry *registry);

/*
 * Library API aliases (ralph_* prefix)
 * These provide the public API as defined in lib/ralph.h
 */

/* Tool registry lifecycle */
#define ralph_tools_init          init_tool_registry
#define ralph_tools_cleanup       cleanup_tool_registry

/* Tool registration */
#define ralph_tools_register      register_tool
#define ralph_tools_register_builtins  register_builtin_tools

/* Tool execution */
#define ralph_tools_execute       execute_tool_call

/* JSON generation */
#define ralph_tools_generate_json       generate_tools_json
#define ralph_tools_generate_anthropic_json  generate_anthropic_tools_json

/* Tool call parsing */
#define ralph_tools_parse_calls         parse_tool_calls
#define ralph_tools_parse_anthropic     parse_anthropic_tool_calls

/* Cleanup helpers */
#define ralph_tools_cleanup_calls       cleanup_tool_calls
#define ralph_tools_cleanup_results     cleanup_tool_results

/*
 * Tool Set Factory Functions
 *
 * These functions create pre-configured tool registries for different
 * agent types. Each factory returns a ToolRegistry populated with
 * the appropriate tools for that agent mode.
 */

/**
 * Create a tool registry with CLI tools.
 * Includes: file operations, shell, search, memory, todos, etc.
 *
 * @return Initialized registry with CLI tools, or NULL on failure.
 *         Caller must free with ralph_tools_cleanup.
 */
static inline ToolRegistry* ralph_tools_create_cli(void) {
    ToolRegistry* registry = malloc(sizeof(ToolRegistry));
    if (registry == NULL) return NULL;

    init_tool_registry(registry);
    if (register_builtin_tools(registry) != 0) {
        cleanup_tool_registry(registry);
        free(registry);
        return NULL;
    }
    return registry;
}

/**
 * Create an empty tool registry.
 * Use ralph_tools_register to add custom tools.
 *
 * @return Empty initialized registry, or NULL on failure.
 *         Caller must free with ralph_tools_cleanup.
 */
static inline ToolRegistry* ralph_tools_create_empty(void) {
    ToolRegistry* registry = malloc(sizeof(ToolRegistry));
    if (registry == NULL) return NULL;

    init_tool_registry(registry);
    return registry;
}

/**
 * Destroy a tool registry created by a factory function.
 *
 * @param registry Registry to destroy (may be NULL)
 */
static inline void ralph_tools_destroy(ToolRegistry* registry) {
    if (registry == NULL) return;
    cleanup_tool_registry(registry);
    free(registry);
}

#endif /* LIB_TOOLS_TOOLS_H */
