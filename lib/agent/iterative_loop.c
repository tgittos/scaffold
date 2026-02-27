#include "iterative_loop.h"
#include "api_round_trip.h"
#include "tool_batch_executor.h"
#include "conversation_state.h"
#include "tool_orchestration.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../ui/status_line.h"
#include "../util/debug_output.h"
#include "../session/token_manager.h"
#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int iterative_loop_run(AgentSession* session, ToolOrchestrationContext* ctx) {
    if (session == NULL || ctx == NULL) {
        return -1;
    }

    int loop_count = 0;

    debug_printf("Starting iterative tool calling loop\n");

    while (1) {
        loop_count++;
        if (loop_count > ITERATIVE_LOOP_MAX_ITERATIONS) {
            fprintf(stderr, "Error: Iterative tool loop exceeded safety limit of %d iterations\n",
                    ITERATIVE_LOOP_MAX_ITERATIONS);
            return -1;
        }
        tool_orchestration_reset_batch(ctx);
        debug_printf("Tool calling loop iteration %d\n", loop_count);

        TokenConfig token_config;
        token_config_init(&token_config, session->session_data.config.context_window);
        TokenUsage token_usage;
        int token_rc = manage_conversation_tokens(session, "", &token_config, &token_usage);
        if (token_rc == SESSION_CONTEXT_FULL) {
            debug_printf("iterative_loop: context full, propagating\n");
            return SESSION_CONTEXT_FULL;
        }
        if (token_rc != 0) {
            fprintf(stderr, "Error: Failed to calculate token allocation for tool loop iteration %d\n", loop_count);
            return -1;
        }

        int iteration_max_tokens = token_usage.available_response_tokens;
        debug_printf("Using %d max_tokens for tool loop iteration %d\n", iteration_max_tokens, loop_count);

        LLMRoundTripResult rt;
        debug_printf("Making API request for tool loop iteration %d\n", loop_count);
        if (api_round_trip_execute(session, "", iteration_max_tokens, &rt) != 0) {
            return -1;
        }

        const char* assistant_content = rt.parsed.response_content ?
                                        rt.parsed.response_content :
                                        rt.parsed.thinking_content;

        ToolCall *tool_calls = rt.tool_calls;
        int call_count = rt.tool_call_count;
        rt.tool_calls = NULL;
        rt.tool_call_count = 0;

        if (call_count > 0) {
            if (rt.parsed.response_content != NULL && strlen(rt.parsed.response_content) > 0) {
                if (!session->session_data.config.json_output_mode) {
                    display_streaming_text(rt.parsed.response_content,
                                           strlen(rt.parsed.response_content));
                } else {
                    json_output_assistant_text(rt.parsed.response_content,
                                               rt.parsed.prompt_tokens,
                                               rt.parsed.completion_tokens);
                }
            }

            if (!session->session_data.config.json_output_mode) {
                display_streaming_complete(rt.parsed.prompt_tokens,
                                           rt.parsed.completion_tokens);
            } else {
                json_output_assistant_tool_calls_buffered(tool_calls, call_count,
                                                          rt.parsed.prompt_tokens,
                                                          rt.parsed.completion_tokens);
            }

            conversation_append_assistant(session, assistant_content, tool_calls, call_count);
        } else {
            conversation_append_assistant(session, assistant_content, NULL, 0);

            if (assistant_content != NULL && session->session_data.config.json_output_mode) {
                json_output_assistant_text(assistant_content,
                                           rt.parsed.prompt_tokens,
                                           rt.parsed.completion_tokens);
            }
        }

        if (call_count == 0) {
            debug_printf("No more tool calls found - ending tool loop after %d iterations\n", loop_count);
            if (!session->session_data.config.json_output_mode) {
                if (rt.parsed.thinking_content != NULL) {
                    display_streaming_thinking(rt.parsed.thinking_content,
                                               strlen(rt.parsed.thinking_content));
                }
                if (rt.parsed.response_content != NULL) {
                    display_streaming_text(rt.parsed.response_content,
                                           strlen(rt.parsed.response_content));
                }
                display_streaming_complete(rt.parsed.prompt_tokens,
                                           rt.parsed.completion_tokens);
            }
            api_round_trip_cleanup(&rt);
            return 0;
        }

        api_round_trip_cleanup(&rt);
        assistant_content = NULL;  /* borrowed from rt; invalidated by cleanup */

        int new_tool_calls = 0;
        for (int i = 0; i < call_count; i++) {
            if (!tool_orchestration_is_duplicate(ctx, tool_calls[i].id)) {
                new_tool_calls++;
            }
        }

        if (new_tool_calls == 0) {
            debug_printf("All %d tool calls already executed - ending loop to prevent infinite iteration\n", call_count);
            cleanup_tool_calls(tool_calls, call_count);
            return 0;
        }

        debug_printf("Found %d new tool calls (out of %d total) in iteration %d - executing them\n",
                    new_tool_calls, call_count, loop_count);

        ToolResult *results = calloc(call_count, sizeof(ToolResult));
        if (results == NULL) {
            cleanup_tool_calls(tool_calls, call_count);
            return -1;
        }

        int *tool_call_indices = malloc(call_count * sizeof(int));
        if (tool_call_indices == NULL) {
            free(results);
            cleanup_tool_calls(tool_calls, call_count);
            return -1;
        }

        ToolBatchContext batch_ctx = { .session = session, .orchestration = ctx };
        int executed_count = 0;
        int batch_status = tool_batch_execute(&batch_ctx, tool_calls, call_count,
                                               results, tool_call_indices, &executed_count);

        conversation_append_tool_results(session, results, executed_count,
                                         tool_calls, tool_call_indices);

        if (batch_status != 0) {
            free(tool_call_indices);
            cleanup_tool_results(results, executed_count);
            cleanup_tool_calls(tool_calls, call_count);
            return batch_status;
        }

        free(tool_call_indices);
        cleanup_tool_results(results, executed_count);
        cleanup_tool_calls(tool_calls, call_count);
    }
}
