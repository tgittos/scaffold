/**
 * lib/tools/tool_extension.h - Tool Extension Interface
 *
 * Provides a callback interface for extending the tool system with external
 * tools (e.g., Python tools from src/). This allows lib/ to remain independent
 * of specific tool implementations while still integrating their functionality.
 *
 * Extensions can provide:
 * - Tool initialization/shutdown lifecycle
 * - Tool registration with the registry
 * - Metadata queries for approval gates and prompt generation
 */

#ifndef LIB_TOOLS_TOOL_EXTENSION_H
#define LIB_TOOLS_TOOL_EXTENSION_H

#include "tools_system.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Metadata callbacks for approval gate and prompt integration.
 * These allow lib/ to query tool metadata without knowing the tool implementation.
 */
typedef struct {
    /**
     * Check if a tool name belongs to this extension.
     * @param name Tool name to check
     * @return 1 if this extension owns the tool, 0 otherwise
     */
    int (*is_extension_tool)(const char *name);

    /**
     * Get the gate category for a tool (e.g., "file_read", "shell").
     * @param name Tool name
     * @return Gate category string, or NULL if not specified
     */
    const char* (*get_gate_category)(const char *name);

    /**
     * Get the argument name for pattern matching in approval gates.
     * @param name Tool name
     * @return Argument name (e.g., "path", "command"), or NULL if not specified
     */
    const char* (*get_match_arg)(const char *name);

    /**
     * Get a description of all tools provided by this extension for the system prompt.
     * @return Dynamically allocated string (caller must free), or NULL if none
     */
    char* (*get_tools_description)(void);
} ToolExtensionMetadata;

/**
 * Full extension interface for external tool systems.
 */
typedef struct {
    /** Extension name (e.g., "python") - for debugging and identification */
    const char* name;

    /**
     * Initialize the extension. Called once at startup.
     * @return 0 on success, -1 on failure
     */
    int (*init)(void);

    /**
     * Register tools with the registry. Called after init().
     * @param registry Tool registry to register with
     * @return 0 on success, -1 on failure
     */
    int (*register_tools)(ToolRegistry *registry);

    /**
     * Shutdown the extension. Called once at cleanup.
     */
    void (*shutdown)(void);

    /** Metadata callbacks for approval gate and prompt integration */
    ToolExtensionMetadata metadata;
} ToolExtension;

/**
 * Register a tool extension with the system.
 * Extensions should be registered before tool_extension_init_all() is called.
 *
 * @param extension Extension to register (structure is copied)
 * @return 0 on success, -1 if max extensions reached
 */
int tool_extension_register(const ToolExtension *extension);

/**
 * Unregister all extensions. Called during cleanup.
 */
void tool_extension_unregister_all(void);

/**
 * Initialize all registered extensions and register their tools.
 * Called from builtin_tools.c during tool registration.
 *
 * @param registry Tool registry for extensions to register their tools
 * @return 0 on success, -1 if any extension failed (continues with others)
 */
int tool_extension_init_all(ToolRegistry *registry);

/**
 * Shutdown all registered extensions.
 * Called from session_cleanup().
 */
void tool_extension_shutdown_all(void);

/* =============================================================================
 * QUERY INTERFACE
 * These functions aggregate results from all registered extensions.
 * Used by approval_gate.c and prompt_loader.c
 * ============================================================================= */

/**
 * Check if a tool name belongs to any registered extension.
 * @param name Tool name to check
 * @return 1 if any extension owns the tool, 0 otherwise
 */
int tool_extension_is_extension_tool(const char *name);

/**
 * Get the gate category for a tool from any registered extension.
 * @param name Tool name
 * @return Gate category string, or NULL if not specified by any extension
 */
const char* tool_extension_get_gate_category(const char *name);

/**
 * Get the match argument for a tool from any registered extension.
 * @param name Tool name
 * @return Argument name, or NULL if not specified by any extension
 */
const char* tool_extension_get_match_arg(const char *name);

/**
 * Get combined tool descriptions from all extensions for the system prompt.
 * @return Dynamically allocated string (caller must free), or NULL if none
 */
char* tool_extension_get_tools_description(void);

#ifdef __cplusplus
}
#endif

#endif /* LIB_TOOLS_TOOL_EXTENSION_H */
