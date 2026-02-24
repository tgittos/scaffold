#include "hook_dispatcher.h"
#include "../agent/session.h"
#include "../util/debug_output.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Check whether a plugin subscribes to a named hook */
static int plugin_has_hook(const PluginProcess *plugin, const char *hook_name) {
    for (int i = 0; i < plugin->manifest.hook_count; i++) {
        if (strcmp(plugin->manifest.hooks[i], hook_name) == 0) return 1;
    }
    return 0;
}

/* Build a sorted index array of plugins subscribing to a hook */
static int *get_sorted_subscribers(PluginManager *mgr, const char *hook_name, int *out_count) {
    *out_count = 0;
    if (!mgr || mgr->count == 0) return NULL;

    int *indices = calloc(mgr->count, sizeof(int));
    if (!indices) return NULL;

    int count = 0;
    for (int i = 0; i < mgr->count; i++) {
        if (!mgr->plugins[i].initialized) continue;
        if (plugin_has_hook(&mgr->plugins[i], hook_name)) {
            indices[count++] = i;
        }
    }

    if (count == 0) {
        free(indices);
        return NULL;
    }

    /* Simple insertion sort by priority (small N) */
    for (int i = 1; i < count; i++) {
        int key = indices[i];
        int j = i - 1;
        while (j >= 0 && mgr->plugins[indices[j]].manifest.priority >
                          mgr->plugins[key].manifest.priority) {
            indices[j + 1] = indices[j];
            j--;
        }
        indices[j + 1] = key;
    }

    *out_count = count;
    return indices;
}

/* Send a hook event to a plugin with proper request ID */
static int send_hook_event(PluginProcess *plugin, const char *json_template, char **response) {
    cJSON *root = cJSON_Parse(json_template);
    if (!root) return -1;

    cJSON_ReplaceItemInObject(root, "id", cJSON_CreateNumber(plugin->request_id++));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;

    int rc = plugin_manager_send_request(plugin, json, response);
    free(json);
    return rc;
}

HookAction hook_dispatch_post_user_input(PluginManager *mgr,
                                          struct AgentSession *session,
                                          char **message) {
    (void)session;
    if (!mgr || !message || !*message) return HOOK_CONTINUE;

    int count = 0;
    int *subs = get_sorted_subscribers(mgr, "post_user_input", &count);
    if (!subs) return HOOK_CONTINUE;

    HookAction final_action = HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "message", *message);

        char *event = plugin_protocol_build_hook_event("post_user_input", params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: post_user_input hook timeout/error\n", p->manifest.name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (hr.action == HOOK_SKIP) {
            cJSON_Delete(hr.data);
            final_action = HOOK_SKIP;
            break;
        }

        if (hr.data) {
            cJSON *msg = cJSON_GetObjectItem(hr.data, "message");
            if (msg && cJSON_IsString(msg)) {
                free(*message);
                *message = strdup(cJSON_GetStringValue(msg));
            }
            cJSON_Delete(hr.data);
        }

        if (hr.action == HOOK_STOP) {
            final_action = HOOK_STOP;
            break;
        }
    }

    free(subs);
    return final_action;
}

HookAction hook_dispatch_context_enhance(PluginManager *mgr,
                                          const struct AgentSession *session,
                                          const char *user_message,
                                          char **dynamic_context) {
    (void)session;
    if (!mgr || !dynamic_context) return HOOK_CONTINUE;

    int count = 0;
    int *subs = get_sorted_subscribers(mgr, "context_enhance", &count);
    if (!subs) return HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = cJSON_CreateObject();
        if (user_message) {
            cJSON_AddStringToObject(params, "user_message", user_message);
        }
        cJSON_AddStringToObject(params, "dynamic_context",
                                *dynamic_context ? *dynamic_context : "");

        char *event = plugin_protocol_build_hook_event("context_enhance", params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: context_enhance hook timeout/error\n", p->manifest.name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (hr.data) {
            cJSON *ctx = cJSON_GetObjectItem(hr.data, "dynamic_context");
            if (ctx && cJSON_IsString(ctx)) {
                free(*dynamic_context);
                *dynamic_context = strdup(cJSON_GetStringValue(ctx));
            }
            cJSON_Delete(hr.data);
        }
        /* Context enhance ignores stop/skip actions */
    }

    free(subs);
    return HOOK_CONTINUE;
}

HookAction hook_dispatch_pre_llm_send(PluginManager *mgr,
                                       const struct AgentSession *session,
                                       char **base_prompt,
                                       char **dynamic_context) {
    (void)session;
    if (!mgr) return HOOK_CONTINUE;

    int count = 0;
    int *subs = get_sorted_subscribers(mgr, "pre_llm_send", &count);
    if (!subs) return HOOK_CONTINUE;

    HookAction final_action = HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "base_prompt",
                                (base_prompt && *base_prompt) ? *base_prompt : "");
        cJSON_AddStringToObject(params, "dynamic_context",
                                (dynamic_context && *dynamic_context) ? *dynamic_context : "");

        char *event = plugin_protocol_build_hook_event("pre_llm_send", params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: pre_llm_send hook timeout/error\n", p->manifest.name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (hr.data) {
            if (base_prompt) {
                cJSON *bp = cJSON_GetObjectItem(hr.data, "base_prompt");
                if (bp && cJSON_IsString(bp)) {
                    free(*base_prompt);
                    *base_prompt = strdup(cJSON_GetStringValue(bp));
                }
            }
            if (dynamic_context) {
                cJSON *dc = cJSON_GetObjectItem(hr.data, "dynamic_context");
                if (dc && cJSON_IsString(dc)) {
                    free(*dynamic_context);
                    *dynamic_context = strdup(cJSON_GetStringValue(dc));
                }
            }
            cJSON_Delete(hr.data);
        }

        if (hr.action == HOOK_STOP) {
            final_action = HOOK_STOP;
            break;
        }
    }

    free(subs);
    return final_action;
}

HookAction hook_dispatch_post_llm_response(PluginManager *mgr,
                                            struct AgentSession *session,
                                            char **text,
                                            const ToolCall *tool_calls,
                                            int call_count) {
    (void)session;
    if (!mgr) return HOOK_CONTINUE;

    int count = 0;
    int *subs = get_sorted_subscribers(mgr, "post_llm_response", &count);
    if (!subs) return HOOK_CONTINUE;

    HookAction final_action = HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "text", (text && *text) ? *text : "");

        cJSON *tc_array = cJSON_CreateArray();
        for (int j = 0; j < call_count; j++) {
            cJSON *tc = cJSON_CreateObject();
            if (tool_calls[j].name) cJSON_AddStringToObject(tc, "name", tool_calls[j].name);
            if (tool_calls[j].arguments) cJSON_AddStringToObject(tc, "arguments", tool_calls[j].arguments);
            cJSON_AddItemToArray(tc_array, tc);
        }
        cJSON_AddItemToObject(params, "tool_calls", tc_array);

        char *event = plugin_protocol_build_hook_event("post_llm_response", params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: post_llm_response hook timeout/error\n", p->manifest.name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (hr.data && text) {
            cJSON *t = cJSON_GetObjectItem(hr.data, "text");
            if (t && cJSON_IsString(t)) {
                free(*text);
                *text = strdup(cJSON_GetStringValue(t));
            }
            cJSON_Delete(hr.data);
        } else {
            cJSON_Delete(hr.data);
        }

        if (hr.action == HOOK_STOP) {
            final_action = HOOK_STOP;
            break;
        }
    }

    free(subs);
    return final_action;
}

HookAction hook_dispatch_pre_tool_execute(PluginManager *mgr,
                                           struct AgentSession *session,
                                           ToolCall *call,
                                           ToolResult *result) {
    (void)session;
    if (!mgr || !call) return HOOK_CONTINUE;

    int count = 0;
    int *subs = get_sorted_subscribers(mgr, "pre_tool_execute", &count);
    if (!subs) return HOOK_CONTINUE;

    HookAction final_action = HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = cJSON_CreateObject();
        if (call->name) cJSON_AddStringToObject(params, "tool_name", call->name);
        if (call->arguments) cJSON_AddStringToObject(params, "arguments", call->arguments);

        char *event = plugin_protocol_build_hook_event("pre_tool_execute", params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: pre_tool_execute hook timeout/error\n", p->manifest.name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (hr.action == HOOK_STOP && result) {
            cJSON *res = hr.data ? cJSON_GetObjectItem(hr.data, "result") : NULL;
            result->tool_call_id = call->id ? strdup(call->id) : NULL;
            result->result = (res && cJSON_IsString(res))
                                 ? strdup(cJSON_GetStringValue(res))
                                 : strdup("{\"blocked\":\"Plugin blocked execution\"}");
            result->success = 0;
            cJSON_Delete(hr.data);
            final_action = HOOK_STOP;
            break;
        }

        cJSON_Delete(hr.data);
    }

    free(subs);
    return final_action;
}

HookAction hook_dispatch_post_tool_execute(PluginManager *mgr,
                                            struct AgentSession *session,
                                            const ToolCall *call,
                                            ToolResult *result) {
    (void)session;
    if (!mgr || !call || !result) return HOOK_CONTINUE;

    int count = 0;
    int *subs = get_sorted_subscribers(mgr, "post_tool_execute", &count);
    if (!subs) return HOOK_CONTINUE;

    HookAction final_action = HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = cJSON_CreateObject();
        if (call->name) cJSON_AddStringToObject(params, "tool_name", call->name);
        if (call->arguments) cJSON_AddStringToObject(params, "arguments", call->arguments);
        if (result->result) cJSON_AddStringToObject(params, "result", result->result);
        cJSON_AddBoolToObject(params, "success", result->success);

        char *event = plugin_protocol_build_hook_event("post_tool_execute", params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: post_tool_execute hook timeout/error\n", p->manifest.name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (hr.data) {
            cJSON *res = cJSON_GetObjectItem(hr.data, "result");
            if (res && cJSON_IsString(res)) {
                free(result->result);
                result->result = strdup(cJSON_GetStringValue(res));
            }
            cJSON_Delete(hr.data);
        }

        if (hr.action == HOOK_STOP) {
            final_action = HOOK_STOP;
            break;
        }
    }

    free(subs);
    return final_action;
}
