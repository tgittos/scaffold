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

/* Send a hook event to a plugin, thread-safe via plugin_send_stamped_request */
static int send_hook_event(PluginProcess *plugin, const char *json_template, char **response) {
    return plugin_send_stamped_request(plugin, json_template, response);
}

/* ========================================================================
 * Generic hook dispatch
 *
 * Each hook provides:
 *   build_params() — construct the JSON params for the hook event
 *   apply_result()  — extract response data and apply it to the caller's state
 *
 * The generic loop handles subscriber ordering, event send/receive, response
 * parsing, and STOP/SKIP/CONTINUE chain semantics.
 * ======================================================================== */

typedef cJSON *(*HookBuildParams)(void *ctx);
typedef void (*HookApplyResult)(void *ctx, const HookResponse *hr);

typedef struct {
    const char *hook_name;
    HookBuildParams build_params;
    HookApplyResult apply_result;
    int ignore_stop_skip;
} HookDispatchSpec;

static HookAction hook_dispatch_generic(PluginManager *mgr,
                                         const HookDispatchSpec *spec,
                                         void *ctx) {
    int count = 0;
    int *subs = get_sorted_subscribers(mgr, spec->hook_name, &count);
    if (!subs) return HOOK_CONTINUE;

    HookAction final_action = HOOK_CONTINUE;

    for (int i = 0; i < count; i++) {
        PluginProcess *p = &mgr->plugins[subs[i]];

        cJSON *params = spec->build_params(ctx);
        char *event = plugin_protocol_build_hook_event(spec->hook_name, params);
        cJSON_Delete(params);
        if (!event) continue;

        char *response = NULL;
        if (send_hook_event(p, event, &response) != 0) {
            free(event);
            debug_printf("Plugin %s: %s hook timeout/error\n",
                         p->manifest.name, spec->hook_name);
            continue;
        }
        free(event);

        HookResponse hr;
        if (plugin_protocol_parse_hook_response(response, &hr) != 0) {
            free(response);
            continue;
        }
        free(response);

        if (!spec->ignore_stop_skip && hr.action == HOOK_SKIP) {
            cJSON_Delete(hr.data);
            final_action = HOOK_SKIP;
            break;
        }

        if (spec->apply_result) {
            spec->apply_result(ctx, &hr);
        }
        cJSON_Delete(hr.data);

        if (!spec->ignore_stop_skip && hr.action == HOOK_STOP) {
            final_action = HOOK_STOP;
            break;
        }
    }

    free(subs);
    return final_action;
}

/* ========================================================================
 * Hook-specific contexts, param builders, and result appliers
 * ======================================================================== */

/* --- post_user_input --- */

typedef struct {
    char **message;
} PostUserInputCtx;

static cJSON *post_user_input_build(void *raw) {
    PostUserInputCtx *ctx = raw;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "message", *ctx->message);
    return params;
}

static void post_user_input_apply(void *raw, const HookResponse *hr) {
    PostUserInputCtx *ctx = raw;
    if (hr->data) {
        cJSON *msg = cJSON_GetObjectItem(hr->data, "message");
        if (msg && cJSON_IsString(msg)) {
            char *new_val = strdup(cJSON_GetStringValue(msg));
            if (new_val) {
                free(*ctx->message);
                *ctx->message = new_val;
            }
        }
    }
}

HookAction hook_dispatch_post_user_input(PluginManager *mgr,
                                          struct AgentSession *session,
                                          char **message) {
    (void)session;
    if (!mgr || !message || !*message) return HOOK_CONTINUE;

    PostUserInputCtx ctx = { .message = message };
    HookDispatchSpec spec = {
        .hook_name = "post_user_input",
        .build_params = post_user_input_build,
        .apply_result = post_user_input_apply,
        .ignore_stop_skip = 0
    };
    return hook_dispatch_generic(mgr, &spec, &ctx);
}

/* --- context_enhance --- */

typedef struct {
    const char *user_message;
    char **dynamic_context;
} ContextEnhanceCtx;

static cJSON *context_enhance_build(void *raw) {
    ContextEnhanceCtx *ctx = raw;
    cJSON *params = cJSON_CreateObject();
    if (ctx->user_message) {
        cJSON_AddStringToObject(params, "user_message", ctx->user_message);
    }
    cJSON_AddStringToObject(params, "dynamic_context",
                            *ctx->dynamic_context ? *ctx->dynamic_context : "");
    return params;
}

static void context_enhance_apply(void *raw, const HookResponse *hr) {
    ContextEnhanceCtx *ctx = raw;
    if (hr->data) {
        cJSON *dc = cJSON_GetObjectItem(hr->data, "dynamic_context");
        if (dc && cJSON_IsString(dc)) {
            char *new_val = strdup(cJSON_GetStringValue(dc));
            if (new_val) {
                free(*ctx->dynamic_context);
                *ctx->dynamic_context = new_val;
            }
        }
    }
}

HookAction hook_dispatch_context_enhance(PluginManager *mgr,
                                          const struct AgentSession *session,
                                          const char *user_message,
                                          char **dynamic_context) {
    (void)session;
    if (!mgr || !dynamic_context) return HOOK_CONTINUE;

    ContextEnhanceCtx ctx = {
        .user_message = user_message,
        .dynamic_context = dynamic_context
    };
    HookDispatchSpec spec = {
        .hook_name = "context_enhance",
        .build_params = context_enhance_build,
        .apply_result = context_enhance_apply,
        .ignore_stop_skip = 1
    };
    return hook_dispatch_generic(mgr, &spec, &ctx);
}

/* --- pre_llm_send --- */

typedef struct {
    char **base_prompt;
    char **dynamic_context;
} PreLlmSendCtx;

static cJSON *pre_llm_send_build(void *raw) {
    PreLlmSendCtx *ctx = raw;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "base_prompt",
                            (ctx->base_prompt && *ctx->base_prompt) ? *ctx->base_prompt : "");
    cJSON_AddStringToObject(params, "dynamic_context",
                            (ctx->dynamic_context && *ctx->dynamic_context) ? *ctx->dynamic_context : "");
    return params;
}

static void pre_llm_send_apply(void *raw, const HookResponse *hr) {
    PreLlmSendCtx *ctx = raw;
    if (hr->data) {
        if (ctx->base_prompt) {
            cJSON *bp = cJSON_GetObjectItem(hr->data, "base_prompt");
            if (bp && cJSON_IsString(bp)) {
                char *new_val = strdup(cJSON_GetStringValue(bp));
                if (new_val) {
                    free(*ctx->base_prompt);
                    *ctx->base_prompt = new_val;
                }
            }
        }
        if (ctx->dynamic_context) {
            cJSON *dc = cJSON_GetObjectItem(hr->data, "dynamic_context");
            if (dc && cJSON_IsString(dc)) {
                char *new_val = strdup(cJSON_GetStringValue(dc));
                if (new_val) {
                    free(*ctx->dynamic_context);
                    *ctx->dynamic_context = new_val;
                }
            }
        }
    }
}

HookAction hook_dispatch_pre_llm_send(PluginManager *mgr,
                                       const struct AgentSession *session,
                                       char **base_prompt,
                                       char **dynamic_context) {
    (void)session;
    if (!mgr) return HOOK_CONTINUE;

    PreLlmSendCtx ctx = {
        .base_prompt = base_prompt,
        .dynamic_context = dynamic_context
    };
    HookDispatchSpec spec = {
        .hook_name = "pre_llm_send",
        .build_params = pre_llm_send_build,
        .apply_result = pre_llm_send_apply,
        .ignore_stop_skip = 0
    };
    return hook_dispatch_generic(mgr, &spec, &ctx);
}

/* --- post_llm_response --- */

typedef struct {
    char **text;
    const ToolCall *tool_calls;
    int call_count;
} PostLlmResponseCtx;

static cJSON *post_llm_response_build(void *raw) {
    PostLlmResponseCtx *ctx = raw;
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "text",
                            (ctx->text && *ctx->text) ? *ctx->text : "");

    cJSON *tc_array = cJSON_CreateArray();
    for (int j = 0; j < ctx->call_count; j++) {
        cJSON *tc = cJSON_CreateObject();
        if (ctx->tool_calls[j].name)
            cJSON_AddStringToObject(tc, "name", ctx->tool_calls[j].name);
        if (ctx->tool_calls[j].arguments)
            cJSON_AddStringToObject(tc, "arguments", ctx->tool_calls[j].arguments);
        cJSON_AddItemToArray(tc_array, tc);
    }
    cJSON_AddItemToObject(params, "tool_calls", tc_array);
    return params;
}

static void post_llm_response_apply(void *raw, const HookResponse *hr) {
    PostLlmResponseCtx *ctx = raw;
    if (hr->data && ctx->text) {
        cJSON *t = cJSON_GetObjectItem(hr->data, "text");
        if (t && cJSON_IsString(t)) {
            char *new_val = strdup(cJSON_GetStringValue(t));
            if (new_val) {
                free(*ctx->text);
                *ctx->text = new_val;
            }
        }
    }
}

HookAction hook_dispatch_post_llm_response(PluginManager *mgr,
                                            struct AgentSession *session,
                                            char **text,
                                            const ToolCall *tool_calls,
                                            int call_count) {
    (void)session;
    if (!mgr) return HOOK_CONTINUE;

    PostLlmResponseCtx ctx = {
        .text = text,
        .tool_calls = tool_calls,
        .call_count = call_count
    };
    HookDispatchSpec spec = {
        .hook_name = "post_llm_response",
        .build_params = post_llm_response_build,
        .apply_result = post_llm_response_apply,
        .ignore_stop_skip = 0
    };
    return hook_dispatch_generic(mgr, &spec, &ctx);
}

/* --- pre_tool_execute --- */

typedef struct {
    ToolCall *call;
    ToolResult *result;
} PreToolExecuteCtx;

static cJSON *pre_tool_execute_build(void *raw) {
    PreToolExecuteCtx *ctx = raw;
    cJSON *params = cJSON_CreateObject();
    if (ctx->call->name) cJSON_AddStringToObject(params, "tool_name", ctx->call->name);
    if (ctx->call->arguments) cJSON_AddStringToObject(params, "arguments", ctx->call->arguments);
    return params;
}

static void pre_tool_execute_apply(void *raw, const HookResponse *hr) {
    PreToolExecuteCtx *ctx = raw;
    if (hr->action == HOOK_STOP && ctx->result) {
        cJSON *res = hr->data ? cJSON_GetObjectItem(hr->data, "result") : NULL;
        ctx->result->tool_call_id = ctx->call->id ? strdup(ctx->call->id) : NULL;
        ctx->result->result = (res && cJSON_IsString(res))
                                 ? strdup(cJSON_GetStringValue(res))
                                 : strdup("{\"blocked\":\"Plugin blocked execution\"}");
        ctx->result->success = 0;
    }
}

HookAction hook_dispatch_pre_tool_execute(PluginManager *mgr,
                                           struct AgentSession *session,
                                           ToolCall *call,
                                           ToolResult *result) {
    (void)session;
    if (!mgr || !call) return HOOK_CONTINUE;

    PreToolExecuteCtx ctx = { .call = call, .result = result };
    HookDispatchSpec spec = {
        .hook_name = "pre_tool_execute",
        .build_params = pre_tool_execute_build,
        .apply_result = pre_tool_execute_apply,
        .ignore_stop_skip = 0
    };
    return hook_dispatch_generic(mgr, &spec, &ctx);
}

/* --- post_tool_execute --- */

typedef struct {
    const ToolCall *call;
    ToolResult *result;
} PostToolExecuteCtx;

static cJSON *post_tool_execute_build(void *raw) {
    PostToolExecuteCtx *ctx = raw;
    cJSON *params = cJSON_CreateObject();
    if (ctx->call->name) cJSON_AddStringToObject(params, "tool_name", ctx->call->name);
    if (ctx->call->arguments) cJSON_AddStringToObject(params, "arguments", ctx->call->arguments);
    if (ctx->result->result) cJSON_AddStringToObject(params, "result", ctx->result->result);
    cJSON_AddBoolToObject(params, "success", ctx->result->success);
    return params;
}

static void post_tool_execute_apply(void *raw, const HookResponse *hr) {
    PostToolExecuteCtx *ctx = raw;
    if (hr->data) {
        cJSON *res = cJSON_GetObjectItem(hr->data, "result");
        if (res && cJSON_IsString(res)) {
            char *new_val = strdup(cJSON_GetStringValue(res));
            if (new_val) {
                free(ctx->result->result);
                ctx->result->result = new_val;
            }
        }
    }
}

HookAction hook_dispatch_post_tool_execute(PluginManager *mgr,
                                            struct AgentSession *session,
                                            const ToolCall *call,
                                            ToolResult *result) {
    (void)session;
    if (!mgr || !call || !result) return HOOK_CONTINUE;

    PostToolExecuteCtx ctx = { .call = call, .result = result };
    HookDispatchSpec spec = {
        .hook_name = "post_tool_execute",
        .build_params = post_tool_execute_build,
        .apply_result = post_tool_execute_apply,
        .ignore_stop_skip = 0
    };
    return hook_dispatch_generic(mgr, &spec, &ctx);
}
