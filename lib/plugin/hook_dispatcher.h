#ifndef LIB_PLUGIN_HOOK_DISPATCHER_H
#define LIB_PLUGIN_HOOK_DISPATCHER_H

#include "plugin_manager.h"
#include "../types.h"

/* Forward declaration to avoid circular includes */
struct AgentSession;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Dispatch post_user_input hook to subscribed plugins.
 * Plugins may transform or skip the user message.
 *
 * @param mgr Plugin manager
 * @param session Agent session
 * @param message In/out: pointer to message string (may be replaced; caller frees)
 * @return HOOK_CONTINUE, HOOK_SKIP, or HOOK_STOP
 */
HookAction hook_dispatch_post_user_input(PluginManager *mgr,
                                          struct AgentSession *session,
                                          char **message);

/**
 * Dispatch context_enhance hook to subscribed plugins.
 * Plugins append to the dynamic context string.
 *
 * @param mgr Plugin manager
 * @param session Agent session
 * @param user_message Current user message
 * @param dynamic_context In/out: pointer to dynamic context (may be replaced; caller frees)
 * @return HOOK_CONTINUE always (stop/skip ignored for context enhancement)
 */
HookAction hook_dispatch_context_enhance(PluginManager *mgr,
                                          const struct AgentSession *session,
                                          const char *user_message,
                                          char **dynamic_context);

/**
 * Dispatch pre_llm_send hook to subscribed plugins.
 * Plugins may modify the base prompt and dynamic context.
 *
 * @param mgr Plugin manager
 * @param session Agent session
 * @param base_prompt In/out: pointer to base prompt (may be replaced; caller frees)
 * @param dynamic_context In/out: pointer to dynamic context (may be replaced; caller frees)
 * @return HOOK_CONTINUE or HOOK_STOP
 */
HookAction hook_dispatch_pre_llm_send(PluginManager *mgr,
                                       const struct AgentSession *session,
                                       char **base_prompt,
                                       char **dynamic_context);

/**
 * Dispatch post_llm_response hook to subscribed plugins.
 * Plugins may transform the response text.
 *
 * @param mgr Plugin manager
 * @param session Agent session
 * @param text In/out: pointer to response text (may be replaced; caller frees)
 * @param tool_calls Tool calls from the response (read-only)
 * @param call_count Number of tool calls
 * @return HOOK_CONTINUE or HOOK_STOP
 */
HookAction hook_dispatch_post_llm_response(PluginManager *mgr,
                                            struct AgentSession *session,
                                            char **text,
                                            const ToolCall *tool_calls,
                                            int call_count);

/**
 * Dispatch pre_tool_execute hook to subscribed plugins.
 * Plugins may block tool execution by returning HOOK_STOP.
 *
 * @param mgr Plugin manager
 * @param session Agent session
 * @param call Tool call about to be executed
 * @param result Pre-allocated result; filled if HOOK_STOP
 * @return HOOK_CONTINUE or HOOK_STOP
 */
HookAction hook_dispatch_pre_tool_execute(PluginManager *mgr,
                                           struct AgentSession *session,
                                           ToolCall *call,
                                           ToolResult *result);

/**
 * Dispatch post_tool_execute hook to subscribed plugins.
 * Plugins may transform the tool result.
 *
 * @param mgr Plugin manager
 * @param session Agent session
 * @param call The tool call that was executed
 * @param result In/out: tool result (may be modified)
 * @return HOOK_CONTINUE or HOOK_STOP
 */
HookAction hook_dispatch_post_tool_execute(PluginManager *mgr,
                                            struct AgentSession *session,
                                            const ToolCall *call,
                                            ToolResult *result);

#ifdef __cplusplus
}
#endif

#endif /* LIB_PLUGIN_HOOK_DISPATCHER_H */
