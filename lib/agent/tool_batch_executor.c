#include "tool_batch_executor.h"
#include "../util/interrupt.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../util/debug_output.h"
#include "../mcp/mcp_client.h"
#include "../ui/spinner.h"
#include "../policy/protected_files.h"
#include "../policy/verified_file_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_error_result(ToolResult* result, const char* call_id,
                               const char* error_json) {
    free(result->tool_call_id);
    free(result->result);
    result->tool_call_id = call_id ? strdup(call_id) : NULL;
    result->result = strdup(error_json);
    result->success = 0;
}

static void fill_remaining_interrupted(ToolBatchContext* ctx,
                                        ToolCall* calls, int call_count,
                                        int start, int compact,
                                        ToolResult* results, int* call_indices,
                                        int* count) {
    int json_mode = ctx->session->session_data.config.json_output_mode;
    for (int j = start; j < call_count; j++) {
        if (compact && tool_orchestration_is_duplicate(ctx->orchestration, calls[j].id)) {
            continue;
        }
        int s = compact ? *count : j;
        if (compact) call_indices[*count] = j;
        fill_error_result(&results[s], calls[j].id,
                          "{\"error\": \"interrupted\", \"message\": \"Cancelled by user\"}");
        if (json_mode) {
            json_output_tool_result(calls[j].id, results[s].result, 1);
        }
        (*count)++;
    }
}

int tool_batch_execute(ToolBatchContext* ctx,
                       ToolCall* calls,
                       int call_count,
                       ToolResult* results,
                       int* call_indices,
                       int* executed_count) {
    if (ctx == NULL || ctx->session == NULL || calls == NULL || executed_count == NULL) {
        return -1;
    }

    int compact = (call_indices != NULL);
    int count = 0;
    int status = 0;
    int json_mode = ctx->session->session_data.config.json_output_mode;

    force_protected_inode_refresh();

    for (int i = 0; i < call_count; i++) {
        if (interrupt_pending()) {
            interrupt_acknowledge();
            spinner_stop();
            status = -2;
            display_cancellation_message(i, call_count, json_mode);
            fill_remaining_interrupted(ctx, calls, call_count, i, compact,
                                       results, call_indices, &count);
            break;
        }

        if (compact) {
            if (tool_orchestration_is_duplicate(ctx->orchestration, calls[i].id)) {
                debug_printf("Skipping already executed tool: %s (ID: %s)\n",
                           calls[i].name, calls[i].id);
                continue;
            }
            if (tool_orchestration_mark_executed(ctx->orchestration, calls[i].id) != 0) {
                debug_printf("Warning: Failed to track tool call ID %s, skipping execution\n",
                           calls[i].id);
                continue;
            }
            call_indices[count] = i;
        }

        int slot = compact ? count : i;

        if (!tool_orchestration_can_spawn_subagent(ctx->orchestration, calls[i].name)) {
            debug_printf("Skipping duplicate subagent call %d (ID: %s)\n", i, calls[i].id);
            fill_error_result(&results[slot], calls[i].id,
                              "{\"error\": \"duplicate_subagent\", \"message\": "
                              "\"Only one subagent can be spawned per turn. "
                              "A subagent was already spawned in this batch.\"}");
            if (!json_mode) {
                log_tool_execution_improved(calls[i].name, calls[i].arguments, 0,
                                            "Duplicate subagent blocked");
            } else {
                json_output_tool_result(calls[i].id, results[slot].result, 1);
            }
            count++;
            continue;
        } else if (strcmp(calls[i].name, "subagent") == 0) {
            debug_printf("First subagent call (ID: %s)\n", calls[i].id);
        }

        int approval = tool_orchestration_check_approval(ctx->orchestration,
                                                          &calls[i], &results[slot]);
        if (approval == -2) {
            status = -1;
            debug_printf("User aborted tool execution at tool %d of %d\n", i + 1, call_count);
            fill_error_result(&results[slot], calls[i].id,
                              "{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
            log_tool_execution_improved(calls[i].name, calls[i].arguments, 0, "Aborted by user");
            count++;
            if (!compact) {
                for (int j = i + 1; j < call_count; j++) {
                    fill_error_result(&results[j], calls[j].id,
                                      "{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
                }
                count = call_count;
            }
            break;
        }
        if (approval == -1) {
            debug_printf("Tool %s blocked by approval gate\n", calls[i].name);
            if (json_mode) {
                json_output_tool_result(calls[i].id, results[slot].result,
                                        !results[slot].success);
            }
            count++;
            continue;
        }

        int tool_executed = 0;
        spinner_start(calls[i].name, calls[i].arguments);

        if (strncmp(calls[i].name, "mcp_", 4) == 0) {
            if (mcp_client_execute_tool(&ctx->session->mcp_client,
                                         &calls[i], &results[slot]) == 0) {
                tool_executed = 1;
            }
        }

        if (!tool_executed &&
            execute_tool_call(&ctx->session->tools, &calls[i], &results[slot]) != 0) {
            fprintf(stderr, "Warning: Failed to execute tool call %s\n", calls[i].name);
            fill_error_result(&results[slot], calls[i].id, "Tool execution failed");
        } else {
            debug_printf("Executed tool: %s (ID: %s)\n", calls[i].name, calls[i].id);
        }

        spinner_stop();

        if (!json_mode) {
            log_tool_execution_improved(calls[i].name, calls[i].arguments,
                                        results[slot].success, results[slot].result);
        }

        verified_file_context_clear();

        if (json_mode) {
            json_output_tool_result(calls[i].id, results[slot].result,
                                    !results[slot].success);
        }

        count++;
    }

    *executed_count = count;
    return status;
}
