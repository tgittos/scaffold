#include "plugin_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 * Message builders
 * -------------------------------------------------------------------------- */

char *plugin_protocol_build_initialize(int protocol_version) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "initialize");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "protocol_version", protocol_version);
    cJSON_AddItemToObject(root, "params", params);

    cJSON_AddNumberToObject(root, "id", 1);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *plugin_protocol_build_hook_event(const char *hook_name, cJSON *params) {
    if (!hook_name) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");

    char method[128];
    snprintf(method, sizeof(method), "hook/%s", hook_name);
    cJSON_AddStringToObject(root, "method", method);

    if (params) {
        cJSON_AddItemToObject(root, "params", cJSON_Duplicate(params, 1));
    } else {
        cJSON_AddItemToObject(root, "params", cJSON_CreateObject());
    }

    /* id=0 is a placeholder; callers should replace with actual request id */
    cJSON_AddNumberToObject(root, "id", 0);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *plugin_protocol_build_tool_execute(const char *name, const char *arguments) {
    if (!name) return NULL;

    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "tool/execute");

    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "name", name);

    if (arguments) {
        cJSON *args = cJSON_Parse(arguments);
        if (args) {
            cJSON_AddItemToObject(params, "arguments", args);
        } else {
            cJSON_AddStringToObject(params, "arguments", arguments);
        }
    } else {
        cJSON_AddItemToObject(params, "arguments", cJSON_CreateObject());
    }

    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddNumberToObject(root, "id", 0);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

char *plugin_protocol_build_shutdown(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "shutdown");
    cJSON_AddItemToObject(root, "params", cJSON_CreateObject());
    cJSON_AddNumberToObject(root, "id", 0);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/* --------------------------------------------------------------------------
 * Response parsers
 * -------------------------------------------------------------------------- */

static ToolParameter *parse_plugin_tool_params(cJSON *params_array, int *count) {
    *count = 0;
    if (!params_array || !cJSON_IsArray(params_array)) return NULL;

    int n = cJSON_GetArraySize(params_array);
    if (n == 0) return NULL;

    ToolParameter *params = calloc(n, sizeof(ToolParameter));
    if (!params) return NULL;

    int parsed = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, params_array) {
        if (!cJSON_IsObject(item)) continue;

        cJSON *name = cJSON_GetObjectItem(item, "name");
        if (!name || !cJSON_IsString(name)) continue;

        params[parsed].name = strdup(cJSON_GetStringValue(name));

        cJSON *type = cJSON_GetObjectItem(item, "type");
        params[parsed].type = (type && cJSON_IsString(type))
                                  ? strdup(cJSON_GetStringValue(type))
                                  : strdup("string");

        cJSON *desc = cJSON_GetObjectItem(item, "description");
        params[parsed].description = (desc && cJSON_IsString(desc))
                                         ? strdup(cJSON_GetStringValue(desc))
                                         : NULL;

        cJSON *req = cJSON_GetObjectItem(item, "required");
        params[parsed].required = (req && cJSON_IsTrue(req)) ? 1 : 0;

        params[parsed].enum_values = NULL;
        params[parsed].enum_count = 0;
        params[parsed].items_schema = NULL;
        parsed++;
    }

    *count = parsed;
    return params;
}

int plugin_protocol_parse_manifest(const char *json, PluginManifest *out) {
    if (!json || !out) return -1;
    memset(out, 0, sizeof(PluginManifest));

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result || !cJSON_IsObject(result)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *name = cJSON_GetObjectItem(result, "name");
    if (!name || !cJSON_IsString(name)) {
        cJSON_Delete(root);
        return -1;
    }
    out->name = strdup(cJSON_GetStringValue(name));

    cJSON *version = cJSON_GetObjectItem(result, "version");
    out->version = (version && cJSON_IsString(version))
                       ? strdup(cJSON_GetStringValue(version))
                       : strdup("0.0.0");

    cJSON *desc = cJSON_GetObjectItem(result, "description");
    out->description = (desc && cJSON_IsString(desc))
                           ? strdup(cJSON_GetStringValue(desc))
                           : strdup("");

    cJSON *priority = cJSON_GetObjectItem(result, "priority");
    out->priority = (priority && cJSON_IsNumber(priority))
                        ? (int)cJSON_GetNumberValue(priority)
                        : 500;

    /* Parse hooks array */
    cJSON *hooks = cJSON_GetObjectItem(result, "hooks");
    if (hooks && cJSON_IsArray(hooks)) {
        int n = cJSON_GetArraySize(hooks);
        if (n > 0) {
            out->hooks = calloc(n, sizeof(char *));
            if (out->hooks) {
                int count = 0;
                cJSON *h = NULL;
                cJSON_ArrayForEach(h, hooks) {
                    if (cJSON_IsString(h)) {
                        out->hooks[count++] = strdup(cJSON_GetStringValue(h));
                    }
                }
                out->hook_count = count;
            }
        }
    }

    /* Parse tools array */
    cJSON *tools = cJSON_GetObjectItem(result, "tools");
    if (tools && cJSON_IsArray(tools)) {
        int n = cJSON_GetArraySize(tools);
        if (n > 0) {
            out->tools = calloc(n, sizeof(ToolFunction));
            if (out->tools) {
                int count = 0;
                cJSON *t = NULL;
                cJSON_ArrayForEach(t, tools) {
                    if (!cJSON_IsObject(t)) continue;

                    cJSON *tname = cJSON_GetObjectItem(t, "name");
                    if (!tname || !cJSON_IsString(tname)) continue;

                    out->tools[count].name = strdup(cJSON_GetStringValue(tname));

                    cJSON *tdesc = cJSON_GetObjectItem(t, "description");
                    out->tools[count].description = (tdesc && cJSON_IsString(tdesc))
                                                        ? strdup(cJSON_GetStringValue(tdesc))
                                                        : NULL;

                    cJSON *tparams = cJSON_GetObjectItem(t, "parameters");
                    out->tools[count].parameters = parse_plugin_tool_params(
                        tparams, &out->tools[count].parameter_count);

                    out->tools[count].execute_func = NULL;
                    out->tools[count].cacheable = 0;
                    out->tools[count].thread_safe = 1;
                    count++;
                }
                out->tool_count = count;
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

int plugin_protocol_parse_hook_response(const char *json, HookResponse *out) {
    if (!json || !out) return -1;
    out->action = HOOK_CONTINUE;
    out->data = NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result || !cJSON_IsObject(result)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *action = cJSON_GetObjectItem(result, "action");
    if (action && cJSON_IsString(action)) {
        const char *action_str = cJSON_GetStringValue(action);
        if (strcmp(action_str, "stop") == 0) {
            out->action = HOOK_STOP;
        } else if (strcmp(action_str, "skip") == 0) {
            out->action = HOOK_SKIP;
        }
    }

    /* Detach result so caller owns it */
    out->data = cJSON_DetachItemFromObject(root, "result");
    cJSON_Delete(root);
    return 0;
}

int plugin_protocol_parse_tool_result(const char *json, PluginToolResult *out) {
    if (!json || !out) return -1;
    out->success = 0;
    out->result = NULL;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *msg = cJSON_GetObjectItem(error, "message");
        out->result = (msg && cJSON_IsString(msg))
                          ? strdup(cJSON_GetStringValue(msg))
                          : strdup("Plugin error");
        out->success = 0;
        cJSON_Delete(root);
        return 0;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (!result || !cJSON_IsObject(result)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *success = cJSON_GetObjectItem(result, "success");
    out->success = (success && cJSON_IsTrue(success)) ? 1 : 0;

    cJSON *res_val = cJSON_GetObjectItem(result, "result");
    if (res_val && cJSON_IsString(res_val)) {
        out->result = strdup(cJSON_GetStringValue(res_val));
    } else if (res_val) {
        out->result = cJSON_PrintUnformatted(res_val);
    } else {
        out->result = strdup("");
    }

    cJSON_Delete(root);
    return 0;
}

/* --------------------------------------------------------------------------
 * Cleanup
 * -------------------------------------------------------------------------- */

void plugin_manifest_cleanup(PluginManifest *manifest) {
    if (!manifest) return;

    free(manifest->name);
    free(manifest->version);
    free(manifest->description);

    for (int i = 0; i < manifest->hook_count; i++) {
        free(manifest->hooks[i]);
    }
    free(manifest->hooks);

    for (int i = 0; i < manifest->tool_count; i++) {
        free(manifest->tools[i].name);
        free(manifest->tools[i].description);
        for (int j = 0; j < manifest->tools[i].parameter_count; j++) {
            free(manifest->tools[i].parameters[j].name);
            free(manifest->tools[i].parameters[j].type);
            free(manifest->tools[i].parameters[j].description);
        }
        free(manifest->tools[i].parameters);
    }
    free(manifest->tools);

    memset(manifest, 0, sizeof(PluginManifest));
}

void plugin_tool_result_cleanup(PluginToolResult *result) {
    if (!result) return;
    free(result->result);
    memset(result, 0, sizeof(PluginToolResult));
}
