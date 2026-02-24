#ifndef LIB_PLUGIN_MANAGER_H
#define LIB_PLUGIN_MANAGER_H

#include "plugin_protocol.h"
#include "../tools/tools_system.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PLUGINS 16
#define PLUGIN_TIMEOUT_MS 5000

typedef struct {
    char *path;
    PluginManifest manifest;
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int initialized;
    int request_id;
} PluginProcess;

typedef struct {
    PluginProcess plugins[MAX_PLUGINS];
    int count;
} PluginManager;

/**
 * Initialize a plugin manager (zero-init).
 */
void plugin_manager_init(PluginManager *mgr);

/**
 * Scan the plugins directory for executables.
 * Populates mgr->plugins[].path for each discovered plugin.
 *
 * @return Number of plugins discovered, or -1 on error
 */
int plugin_manager_discover(PluginManager *mgr);

/**
 * Spawn all discovered plugins, perform handshake, register tools.
 *
 * @param mgr Plugin manager
 * @param registry Tool registry to register plugin-provided tools into
 * @return 0 on success (individual plugin failures are non-fatal)
 */
int plugin_manager_start_all(PluginManager *mgr, ToolRegistry *registry);

/**
 * Gracefully shut down all plugin processes.
 * Sends shutdown message, waits briefly, then SIGTERM/SIGKILL.
 */
void plugin_manager_shutdown_all(PluginManager *mgr);

/**
 * Send a JSON-RPC request to a plugin and receive the response.
 *
 * @param plugin Target plugin process
 * @param json JSON-RPC request string
 * @param response Output: heap-allocated response string (caller frees)
 * @return 0 on success, -1 on error/timeout
 */
int plugin_manager_send_request(PluginProcess *plugin, const char *json, char **response);

/**
 * Execute a plugin-provided tool via IPC.
 *
 * @param mgr Plugin manager
 * @param tool_call Tool call from the LLM
 * @param result Output result
 * @return 0 on success, -1 on error
 */
int plugin_manager_execute_tool(PluginManager *mgr, const ToolCall *tool_call, ToolResult *result);

/**
 * Get the plugins directory path.
 * @return Heap-allocated path string (caller frees), or NULL on error
 */
char *plugin_manager_get_plugins_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* LIB_PLUGIN_MANAGER_H */
