#ifndef LIB_PLUGIN_PROTOCOL_H
#define LIB_PLUGIN_PROTOCOL_H

#include "../tools/tools_system.h"
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PLUGIN_PROTOCOL_VERSION 1

/** Hook action codes returned by plugins */
typedef enum {
    HOOK_CONTINUE = 0,   /**< Continue to next plugin / normal flow */
    HOOK_STOP     = 1,   /**< Stop hook chain, use provided result */
    HOOK_SKIP     = 2    /**< Discard the event (e.g. skip user message) */
} HookAction;

/** Plugin manifest parsed from initialize response */
typedef struct {
    char *name;
    char *version;
    char *description;
    char **hooks;
    int hook_count;
    ToolFunction *tools;
    int tool_count;
    int priority;
} PluginManifest;

/** Parsed hook response from a plugin */
typedef struct {
    HookAction action;
    cJSON *data;         /**< Owned by caller; must be freed with cJSON_Delete */
} HookResponse;

/** Parsed tool execution result from a plugin */
typedef struct {
    int success;
    char *result;
} PluginToolResult;

/* --- Message builders (return heap-allocated JSON strings, caller frees) --- */

char *plugin_protocol_build_initialize(int protocol_version);
char *plugin_protocol_build_hook_event(const char *hook_name, cJSON *params);
char *plugin_protocol_build_tool_execute(const char *name, const char *arguments);
char *plugin_protocol_build_shutdown(void);

/* --- Response parsers --- */

int plugin_protocol_parse_manifest(const char *json, PluginManifest *out);
int plugin_protocol_parse_hook_response(const char *json, HookResponse *out);
int plugin_protocol_parse_tool_result(const char *json, PluginToolResult *out);

/* --- Cleanup --- */

void plugin_manifest_cleanup(PluginManifest *manifest);
void plugin_tool_result_cleanup(PluginToolResult *result);

#ifdef __cplusplus
}
#endif

#endif /* LIB_PLUGIN_PROTOCOL_H */
