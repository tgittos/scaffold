/* lib/ boundary: this file must not depend on src/. */
#include "session.h"
#include "session_configurator.h"
#include "message_dispatcher.h"
#include "message_processor.h"
#include "context_enhancement.h"
#include "tool_executor.h"
#include "streaming_handler.h"
#include "recap.h"
#include "../tools/todo_tool.h"
#include "../tools/todo_display.h"
#include "../tools/builtin_tools.h"
#include "../tools/mode_tool.h"
#include "../tools/tool_extension.h"
#include "../tools/messaging_tool.h"
#include "../util/debug_output.h"
#include "../util/uuid_utils.h"
#include "../session/conversation_tracker.h"
#include "../session/session_manager.h"
#include "../ipc/message_store.h"
#include "../network/api_common.h"
#include "../network/image_attachment.h"
#include "../session/conversation_compactor.h"
#include "../llm/llm_provider.h"
#include "../llm/llm_client.h"
#include "async_executor.h"
#include "../util/config.h"
#include "../plugin/plugin_manager.h"
#include "../plugin/hook_dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_CLEANUP_HOOKS 8
static SessionCleanupHook g_cleanup_hooks[MAX_CLEANUP_HOOKS];
static int g_cleanup_hook_count = 0;

int session_register_cleanup_hook(SessionCleanupHook hook) {
    if (hook == NULL) {
        return -1;
    }
    if (g_cleanup_hook_count >= MAX_CLEANUP_HOOKS) {
        fprintf(stderr, "Warning: Maximum cleanup hooks (%d) reached\n", MAX_CLEANUP_HOOKS);
        return -1;
    }
    g_cleanup_hooks[g_cleanup_hook_count++] = hook;
    return 0;
}

void session_unregister_all_hooks(void) {
    g_cleanup_hook_count = 0;
}

/**
 * Callback invoked when a subagent spawns to notify the async executor's select loop.
 * This allows the main thread to immediately rebuild its fd_set to include the new
 * subagent's approval channel instead of waiting for the next timeout.
 */
static void on_subagent_spawn(void *user_data) {
    (void)user_data;
    async_executor_notify_subagent_spawned(async_executor_get_active());
}

int session_init(AgentSession* session) {
    if (session == NULL) return -1;

    session->model_registry = NULL;
    session->current_mode = PROMPT_MODE_DEFAULT;

    if (uuid_generate_v4(session->session_id) != 0) {
        fprintf(stderr, "Warning: Failed to generate session ID, using fallback\n");
        snprintf(session->session_id, sizeof(session->session_id), "fallback-%ld", (long)time(NULL));
    }

    session->polling_config.auto_poll_enabled = 1;
    session->polling_config.poll_interval_ms = MESSAGE_POLLER_DEFAULT_INTERVAL_MS;
    session->message_poller = NULL;

    session_data_init(&session->session_data);

    if (load_conversation_history(&session->session_data.conversation) != 0) {
        fprintf(stderr, "Error: Failed to load conversation history\n");
        goto cleanup_session_data;
    }

    init_tool_registry(&session->tools);
    if (register_builtin_tools(&session->tools) != 0) {
        fprintf(stderr, "Warning: Failed to register built-in tools\n");
    }

    mode_tool_set_session(session);

    if (tool_extension_init_all(&session->tools) != 0) {
        fprintf(stderr, "Warning: Some tool extensions failed to initialize\n");
    }

    if (todo_list_init(&session->todo_list) != 0) {
        fprintf(stderr, "Error: Failed to initialize todo list\n");
        goto cleanup_tools;
    }

    if (register_todo_tool(&session->tools, &session->todo_list, session->services) != 0) {
        fprintf(stderr, "Warning: Failed to register todo tools\n");
    }

    TodoDisplayConfig display_config = {
        .enabled = true,
        .show_completed = false,
        .compact_mode = true,
        .max_display_items = 5
    };
    if (todo_display_init(&display_config) != 0) {
        fprintf(stderr, "Warning: Failed to initialize todo display\n");
    }

    /* MCP servers are optional; initialization failures are non-fatal */
    if (mcp_client_init(&session->mcp_client) != 0) {
        fprintf(stderr, "Warning: Failed to initialize MCP client\n");
    } else {
        char config_path[512];
        if (mcp_find_config_path(config_path, sizeof(config_path)) == 0) {
            if (mcp_client_load_config(&session->mcp_client, config_path) == 0) {
                if (mcp_client_connect_servers(&session->mcp_client) == 0) {
                    if (mcp_client_register_tools(&session->mcp_client, &session->tools) != 0) {
                        fprintf(stderr, "Warning: Failed to register MCP tools\n");
                    }
                }
            }
        }
    }

    if (subagent_manager_init_with_config(&session->subagent_manager,
                                          SUBAGENT_MAX_DEFAULT,
                                          SUBAGENT_TIMEOUT_DEFAULT) != 0) {
        fprintf(stderr, "Warning: Failed to initialize subagent manager\n");
    } else {
        /* Connect spawn callback for immediate fd_set rebuild in interactive mode */
        subagent_manager_set_spawn_callback(&session->subagent_manager, on_subagent_spawn, NULL);
        if (register_subagent_tool(&session->tools, &session->subagent_manager) != 0) {
            fprintf(stderr, "Warning: Failed to register subagent tool\n");
        }
        if (register_subagent_status_tool(&session->tools, &session->subagent_manager) != 0) {
            fprintf(stderr, "Warning: Failed to register subagent_status tool\n");
        }
    }

    if (llm_client_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize LLM HTTP subsystem\n");
        goto cleanup_subsystems;
    }

    if (approval_gate_init(&session->gate_config) != 0) {
        fprintf(stderr, "Warning: Failed to initialize approval gates\n");
    } else {
        approval_gate_detect_interactive(&session->gate_config);
        /* Set gate_config on subagent manager for approval proxying during blocking waits */
        subagent_manager_set_gate_config(&session->subagent_manager, &session->gate_config);
    }

    /* Plugins are optional; failures are non-fatal */
    plugin_manager_init(&session->plugin_manager);
    plugin_manager_discover(&session->plugin_manager);
    plugin_manager_start_all(&session->plugin_manager, &session->tools);

    return 0;

cleanup_subsystems:
    subagent_manager_cleanup(&session->subagent_manager);
    mcp_client_cleanup(&session->mcp_client);
    clear_todo_tool_reference();
    todo_display_cleanup();
    todo_list_destroy(&session->todo_list);
cleanup_tools:
    tool_extension_shutdown_all();
    cleanup_tool_registry(&session->tools);
cleanup_session_data:
    session_data_cleanup(&session->session_data);
    return -1;
}

void session_cleanup(AgentSession* session) {
    if (session == NULL) return;

    for (int i = g_cleanup_hook_count - 1; i >= 0; i--) {
        if (g_cleanup_hooks[i] != NULL) {
            g_cleanup_hooks[i](session);
        }
    }

    if (session->message_poller != NULL) {
        message_poller_stop(session->message_poller);
        message_poller_destroy(session->message_poller);
        session->message_poller = NULL;
    }

    plugin_manager_shutdown_all(&session->plugin_manager);

    provider_registry_cleanup();
    llm_client_cleanup();

    tool_extension_shutdown_all();

    approval_gate_cleanup(&session->gate_config);
    subagent_manager_cleanup(&session->subagent_manager);
    mcp_client_cleanup(&session->mcp_client);

    messaging_tool_cleanup();
    mode_tool_set_session(NULL);

    /* Cleanup ordering: todo tool holds a pointer to todo_list, which the registry references */
    clear_todo_tool_reference();
    todo_display_cleanup();

    todo_list_destroy(&session->todo_list);
    cleanup_tool_registry(&session->tools);
    session_data_cleanup(&session->session_data);
    config_cleanup();
}

void session_wire_services(AgentSession* session) {
    if (session == NULL) return;

    if (services_get_task_store(session->services) == NULL) {
        fprintf(stderr, "Warning: Task store unavailable, using in-memory tasks only\n");
    }

    if (services_get_message_store(session->services) == NULL) {
        fprintf(stderr, "Warning: Message store unavailable, messaging disabled\n");
    } else {
        messaging_tool_set_services(session->services);
        messaging_tool_set_agent_id(session->session_id);
        const char* parent_id = getenv(RALPH_PARENT_AGENT_ID_ENV);
        if (parent_id != NULL && parent_id[0] != '\0') {
            messaging_tool_set_parent_agent_id(parent_id);
        }
    }
}

int session_load_config(AgentSession* session) {
    if (session == NULL) return -1;
    return session_configurator_load(session);
}

int session_start_message_polling(AgentSession* session) {
    if (session == NULL) {
        return -1;
    }

    if (!session->polling_config.auto_poll_enabled) {
        debug_printf("Message polling disabled by configuration\n");
        return 0;
    }

    if (services_get_message_store(session->services) == NULL) {
        debug_printf("Message store unavailable, skipping message polling\n");
        return 0;
    }

    if (session->message_poller != NULL) {
        return 0;
    }

    session->message_poller = message_poller_create(session->session_id,
                                                     session->polling_config.poll_interval_ms,
                                                     session->services);
    if (session->message_poller == NULL) {
        fprintf(stderr, "Warning: Failed to create message poller\n");
        return -1;
    }

    if (message_poller_start(session->message_poller) != 0) {
        fprintf(stderr, "Warning: Failed to start message poller\n");
        message_poller_destroy(session->message_poller);
        session->message_poller = NULL;
        return -1;
    }

    debug_printf("Message polling started (interval: %dms)\n", session->polling_config.poll_interval_ms);
    return 0;
}

void session_stop_message_polling(AgentSession* session) {
    if (session == NULL || session->message_poller == NULL) {
        return;
    }

    message_poller_stop(session->message_poller);
    message_poller_destroy(session->message_poller);
    session->message_poller = NULL;
    debug_printf("Message polling stopped\n");
}

int session_generate_recap(AgentSession* session, int max_messages) {
    return recap_generate(session, max_messages);
}

int session_execute_tool_workflow(AgentSession* session, ToolCall* tool_calls,
                                   int call_count, const char* user_message,
                                   int max_tokens) {
    return tool_executor_run_workflow(session, tool_calls, call_count,
                                      user_message, max_tokens);
}

char* session_build_json_payload(AgentSession* session,
                                  const char* user_message, int max_tokens) {
    if (session == NULL) return NULL;

    EnhancedPromptParts parts;
    if (build_enhanced_prompt_parts(session, user_message, &parts) != 0) return NULL;

    SystemPromptParts sys_parts = {
        .base_prompt = parts.base_prompt,
        .dynamic_context = parts.dynamic_context
    };

    char* result = build_json_payload_common(session->session_data.config.model, &sys_parts,
                                             &session->session_data.conversation, user_message,
                                             session->session_data.config.max_tokens_param, max_tokens,
                                             &session->tools,
                                             format_openai_message, 0);

    free_enhanced_prompt_parts(&parts);
    return result;
}

char* session_build_anthropic_json_payload(AgentSession* session,
                                            const char* user_message, int max_tokens) {
    if (session == NULL) return NULL;

    EnhancedPromptParts parts;
    if (build_enhanced_prompt_parts(session, user_message, &parts) != 0) return NULL;

    SystemPromptParts sys_parts = {
        .base_prompt = parts.base_prompt,
        .dynamic_context = parts.dynamic_context
    };

    char* result = build_json_payload_common(session->session_data.config.model, &sys_parts,
                                             &session->session_data.conversation, user_message,
                                             "max_tokens", max_tokens,
                                             &session->tools,
                                             format_anthropic_message, 1);

    free_enhanced_prompt_parts(&parts);
    return result;
}

int manage_conversation_tokens(AgentSession* session, const char* user_message,
                               TokenConfig* config, TokenUsage* usage) {
    if (session == NULL || config == NULL || usage == NULL) {
        return -1;
    }

    session->session_data.tool_count = session->tools.functions.count;

    int result = calculate_token_allocation(&session->session_data, user_message, config, usage);
    if (result != 0) {
        return result;
    }

    CompactionConfig compact_config;
    compaction_config_init(&compact_config);

    compact_config.background_threshold = (int)(config->context_window * COMPACTION_TRIGGER_THRESHOLD);

    CompactionResult background_result = {0};
    int background_status = background_compact_conversation(&session->session_data, &compact_config, &background_result);

    if (background_status == 0 && background_result.tokens_saved > 0) {
        debug_printf("Background trimming saved %d tokens, recalculating allocation\n", background_result.tokens_saved);
        cleanup_compaction_result(&background_result);

        result = calculate_token_allocation(&session->session_data, user_message, config, usage);
        if (result != 0) {
            return result;
        }
        debug_printf("After background trimming: %d response tokens available\n", usage->available_response_tokens);
    } else if (background_status == 0) {
        cleanup_compaction_result(&background_result);
    }

    if (usage->available_response_tokens < config->min_response_tokens * 2) {
        debug_printf("Available response tokens (%d) below comfortable threshold, attempting emergency trimming\n",
                     usage->available_response_tokens);

        int target_tokens = (int)(config->context_window * 0.7);

        CompactionResult compact_result = {0};
        int compact_status = compact_conversation(&session->session_data, &compact_config, target_tokens, &compact_result);

        if (compact_status == 0 && compact_result.tokens_saved > 0) {
            debug_printf("Trimming saved %d tokens, recalculating allocation\n", compact_result.tokens_saved);
            cleanup_compaction_result(&compact_result);

            result = calculate_token_allocation(&session->session_data, user_message, config, usage);
            if (result == 0) {
                debug_printf("After trimming: %d response tokens available\n", usage->available_response_tokens);
            }

            return result;
        } else {
            debug_printf("Trimming failed or ineffective, using original allocation\n");
            cleanup_compaction_result(&compact_result);
        }
    }

    return 0;
}

int session_process_message(AgentSession* session, const char* user_message) {
    if (session == NULL || user_message == NULL) return -1;

    /* Parse image attachments from @path references */
    ImageParseResult image_parse;
    int has_images = 0;
    const char *effective_message = user_message;

    if (image_attachment_parse(user_message, &image_parse) == 0) {
        if (image_parse.count > 0) {
            has_images = 1;
            api_common_set_pending_images(image_parse.items, image_parse.count);
            effective_message = image_parse.cleaned_text;
        } else {
            image_attachment_cleanup(&image_parse);
        }
    }

    /* Plugin hook: post_user_input */
    char *hook_msg = strdup(effective_message);
    if (hook_msg) {
        HookAction hr = hook_dispatch_post_user_input(&session->plugin_manager, session, &hook_msg);
        if (hr == HOOK_SKIP) {
            free(hook_msg);
            if (has_images) {
                api_common_clear_pending_images();
                image_attachment_cleanup(&image_parse);
            }
            return 0;
        }
        effective_message = hook_msg;
    }

    TokenConfig token_config;
    token_config_init(&token_config, session->session_data.config.context_window);

    TokenUsage token_usage;
    if (manage_conversation_tokens(session, effective_message, &token_config, &token_usage) != 0) {
        fprintf(stderr, "Error: Failed to calculate token allocation\n");
        if (hook_msg != user_message) free(hook_msg);
        if (has_images) {
            api_common_clear_pending_images();
            image_attachment_cleanup(&image_parse);
        }
        return -1;
    }

    int max_tokens = session->session_data.config.max_tokens;
    if (max_tokens == -1) {
        max_tokens = token_usage.available_response_tokens;
    }

    debug_printf("Using token allocation - Response tokens: %d, Safety buffer: %d, Context window: %d\n",
                 max_tokens, token_usage.safety_buffer_used, token_usage.context_window_used);

    int result;
    DispatchDecision dispatch = message_dispatcher_select_mode(session);
    if (dispatch.mode == DISPATCH_STREAMING) {
        result = streaming_process_message(session, dispatch.provider, effective_message, max_tokens);
    } else {
        LLMRoundTripResult rt;
        if (api_round_trip_execute(session, effective_message, max_tokens, &rt) != 0) {
            if (hook_msg != user_message) free(hook_msg);
            if (has_images) {
                api_common_clear_pending_images();
                image_attachment_cleanup(&image_parse);
            }
            return -1;
        }

        result = message_processor_handle_response(session, &rt, effective_message, max_tokens);
        api_round_trip_cleanup(&rt);
    }

    if (hook_msg != user_message) free(hook_msg);
    if (has_images) {
        api_common_clear_pending_images();
        image_attachment_cleanup(&image_parse);
    }
    return result;
}

int session_continue(AgentSession* session) {
    if (session == NULL) return -1;

    TokenConfig token_config;
    token_config_init(&token_config, session->session_data.config.context_window);

    TokenUsage token_usage;
    if (manage_conversation_tokens(session, NULL, &token_config, &token_usage) != 0) {
        fprintf(stderr, "Error: Failed to calculate token allocation\n");
        return -1;
    }

    int max_tokens = session->session_data.config.max_tokens;
    if (max_tokens == -1) {
        max_tokens = token_usage.available_response_tokens;
    }

    debug_printf("session_continue: triggering LLM with current conversation\n");

    DispatchDecision dispatch = message_dispatcher_select_mode(session);
    if (dispatch.mode == DISPATCH_STREAMING) {
        return streaming_process_message(session, dispatch.provider, NULL, max_tokens);
    }

    LLMRoundTripResult rt;
    if (api_round_trip_execute(session, NULL, max_tokens, &rt) != 0) {
        return -1;
    }

    int result = message_processor_handle_response(session, &rt, NULL, max_tokens);
    api_round_trip_cleanup(&rt);
    return result;
}
