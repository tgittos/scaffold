#define LOG_MODULE     LOG_MOD_AGENT
#define LOG_MODULE_STR "agent"
#include "../util/log.h"

#include "iterative_loop.h"
#include "api_round_trip.h"
#include "streaming_handler.h"
#include "message_dispatcher.h"
#include "tool_batch_executor.h"
#include "conversation_state.h"
#include "tool_orchestration.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../ui/status_line.h"
#include "../session/token_manager.h"
#include "session.h"
#include "../session/conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STREAM_RETRY_MAX 3

int iterative_loop_run(AgentSession* session, ToolOrchestrationContext* ctx,
                       LoopWorkflowState* wf_state) {
    if (session == NULL || ctx == NULL || wf_state == NULL) {
        return -1;
    }

    int loop_count = 0;
    int stream_retries = 0;

    LOG_INFO("Starting iterative tool calling loop");

    while (1) {
        loop_count++;
        if (loop_count > ITERATIVE_LOOP_MAX_ITERATIONS) {
            fprintf(stderr, "Error: Iterative tool loop exceeded safety limit of %d iterations\n",
                    ITERATIVE_LOOP_MAX_ITERATIONS);
            return -1;
        }
        tool_orchestration_reset_batch(ctx);
        LOG_DEBUG("Tool calling loop iteration %d", loop_count);

        TokenConfig token_config;
        token_config_init(&token_config, session->session_data.config.context_window);
        TokenUsage token_usage;
        int token_rc = manage_conversation_tokens(session, "", &token_config, &token_usage);
        if (token_rc == SESSION_CONTEXT_FULL) {
            LOG_WARN("iterative_loop: context full, propagating");
            return SESSION_CONTEXT_FULL;
        }
        if (token_rc != 0) {
            fprintf(stderr, "Error: Failed to calculate token allocation for tool loop iteration %d\n", loop_count);
            return -1;
        }

        int iteration_max_tokens = token_usage.available_response_tokens;
        LOG_DEBUG("Using %d max_tokens for tool loop iteration %d", iteration_max_tokens, loop_count);

        LLMRoundTripResult rt;
        LOG_DEBUG("Making API request for tool loop iteration %d", loop_count);

        DispatchDecision dispatch = message_dispatcher_select_mode(session);
        int rt_rc;
        if (dispatch.mode == DISPATCH_STREAMING && dispatch.provider != NULL) {
            rt_rc = streaming_round_trip_execute(session, dispatch.provider,
                                                  "", iteration_max_tokens, &rt);
        } else {
            rt_rc = api_round_trip_execute(session, "", iteration_max_tokens, &rt);
        }
        if (rt_rc != 0) {
            stream_retries++;
            if (stream_retries <= STREAM_RETRY_MAX) {
                int delay_ms = 1000 * (1 << (stream_retries - 1));
                LOG_WARN("API request failed in tool loop iteration %d, retry %d/%d in %dms",
                         loop_count, stream_retries, STREAM_RETRY_MAX, delay_ms);
                usleep(delay_ms * 1000);
                continue;
            }
            return -1;
        }
        stream_retries = 0;

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
            LOG_INFO("No more tool calls - ending loop after %d iterations", loop_count);
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

            /* Nudge: if the model used tools but stopped without writing
             * any files, it may have described changes without applying them.
             * Push it back to actually make the changes. */
            if (wf_state->has_used_tools && !wf_state->has_patched &&
                wf_state->nudge_count < ITERATIVE_LOOP_MAX_NUDGES) {
                const char *nudge_msg =
                    "You described changes but didn't apply them. "
                    "Use the apply_patch tool to make your changes to "
                    "the actual files, then run tests to verify.";
                LOG_INFO("Nudging model to apply changes (nudge %d/%d)",
                         wf_state->nudge_count + 1, ITERATIVE_LOOP_MAX_NUDGES);
                append_conversation_message(
                    &session->session_data.conversation, "user", nudge_msg);
                if (session->session_data.config.json_output_mode) {
                    json_output_system("nudge", nudge_msg);
                }
                wf_state->nudge_count++;
                api_round_trip_cleanup(&rt);
                continue;
            }

            /* Nudge: if the model patched but never tested, push it back */
            if (wf_state->has_patched && !wf_state->has_tested_since_patch &&
                wf_state->nudge_count < ITERATIVE_LOOP_MAX_NUDGES) {
                const char *nudge_msg =
                    "You applied a patch but haven't verified it. "
                    "Run the relevant tests to confirm your fix works "
                    "and doesn't break existing behavior.";
                LOG_INFO("Nudging model to test (nudge %d/%d)",
                         wf_state->nudge_count + 1, ITERATIVE_LOOP_MAX_NUDGES);
                append_conversation_message(
                    &session->session_data.conversation, "user", nudge_msg);
                if (session->session_data.config.json_output_mode) {
                    json_output_system("nudge", nudge_msg);
                }
                wf_state->nudge_count++;
                api_round_trip_cleanup(&rt);
                continue;
            }

            /* Nudge: if the model tested but tests failed, push it back */
            if (wf_state->has_patched && wf_state->last_test_failed &&
                wf_state->nudge_count < ITERATIVE_LOOP_MAX_NUDGES) {
                const char *nudge_msg =
                    "Tests are still failing. Your fix is not complete. "
                    "Read the test failures carefully, identify what your "
                    "patch got wrong, and apply a corrected patch. Do not "
                    "stop until the relevant tests pass.";
                LOG_INFO("Nudging model to fix failing tests (nudge %d/%d)",
                         wf_state->nudge_count + 1, ITERATIVE_LOOP_MAX_NUDGES);
                append_conversation_message(
                    &session->session_data.conversation, "user", nudge_msg);
                if (session->session_data.config.json_output_mode) {
                    json_output_system("nudge", nudge_msg);
                }
                wf_state->nudge_count++;
                api_round_trip_cleanup(&rt);
                continue;
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
            LOG_INFO("All %d tool calls already executed - ending loop to prevent infinite iteration", call_count);
            cleanup_tool_calls(tool_calls, call_count);
            return 0;
        }

        LOG_DEBUG("Found %d new tool calls (out of %d total) in iteration %d",
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

        /* Track workflow state: did this batch patch or test?
         * Only count apply_patch/write_file as a real patch if it succeeded —
         * a failed patch should not reset test-tracking state. */
        wf_state->has_used_tools = 1;
        for (int i = 0; i < call_count; i++) {
            if (tool_calls[i].name == NULL) continue;
            if (strcmp(tool_calls[i].name, "apply_patch") == 0 ||
                strcmp(tool_calls[i].name, "write_file") == 0) {
                /* Check if this patch actually succeeded */
                int patch_ok = 0;
                for (int j = 0; j < executed_count; j++) {
                    int idx = tool_call_indices ? tool_call_indices[j] : j;
                    if (idx == i && results[j].result != NULL) {
                        /* Failed patches contain "success": false */
                        if (strstr(results[j].result, "\"success\": false") == NULL &&
                            strstr(results[j].result, "\"success\":false") == NULL) {
                            patch_ok = 1;
                        }
                        break;
                    }
                }
                if (patch_ok) {
                    wf_state->has_patched = 1;
                    wf_state->has_tested_since_patch = 0;
                    wf_state->last_test_failed = 0;
                }
            } else if (strcmp(tool_calls[i].name, "shell") == 0) {
                wf_state->has_tested_since_patch = 1;
                /* Check if the shell command failed (non-zero exit_code).
                 * The result JSON looks like: {"exit_code": N, ...}
                 * Find the matching result for this tool call. */
                wf_state->last_test_failed = 0;
                for (int j = 0; j < executed_count; j++) {
                    int idx = tool_call_indices ? tool_call_indices[j] : j;
                    if (idx == i && results[j].result != NULL) {
                        const char *ec = strstr(results[j].result, "\"exit_code\":");
                        if (ec != NULL) {
                            /* Skip past "exit_code": and whitespace */
                            ec += strlen("\"exit_code\":");
                            while (*ec == ' ') ec++;
                            int code = atoi(ec);
                            if (code != 0) {
                                wf_state->last_test_failed = 1;
                            }
                        }
                        break;
                    }
                }
            }
        }

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
