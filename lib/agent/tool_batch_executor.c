#include "tool_batch_executor.h"
#include "../util/interrupt.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../util/debug_output.h"
#include "../mcp/mcp_client.h"
#include "../ui/spinner.h"
#include "../policy/protected_files.h"
#include "../policy/verified_file_context.h"
#include "../policy/atomic_file.h"
#include "../plugin/hook_dispatcher.h"
#include <cJSON.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int call_index;
    int slot;
    int is_mcp;
    int approved;
    int thread_safe;
    ApprovedPath approved_path;
    int has_approved_path;
} PreScreenEntry;

typedef struct {
    ToolBatchContext* ctx;
    ToolCall* call;
    ToolResult* result;
    PreScreenEntry* entry;
} WorkerArg;

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

static void execute_single_tool(ToolBatchContext* ctx, ToolCall* call,
                                 ToolResult* result, PreScreenEntry* entry) {
    /* Plugin hook: pre_tool_execute */
    if (hook_dispatch_pre_tool_execute(&ctx->session->plugin_manager,
                                        ctx->session, call, result) == HOOK_STOP) {
        return;
    }

    if (entry->has_approved_path) {
        verified_file_context_set(&entry->approved_path);
    }

    int tool_executed = 0;
    if (entry->is_mcp) {
        if (mcp_client_execute_tool(&ctx->session->mcp_client,
                                     call, result) == 0) {
            tool_executed = 1;
        }
    }

    if (!tool_executed &&
        execute_tool_call(&ctx->session->tools, call, result) != 0) {
        debug_printf("Warning: Failed to execute tool call %s\n", call->name);
        fill_error_result(result, call->id, "Tool execution failed");
    } else {
        debug_printf("Executed tool: %s (ID: %s)\n", call->name, call->id);
    }

    verified_file_context_clear();

    /* Plugin hook: post_tool_execute */
    hook_dispatch_post_tool_execute(&ctx->session->plugin_manager,
                                    ctx->session, call, result);
}

static void* worker_thread(void* arg) {
    WorkerArg* w = (WorkerArg*)arg;
    execute_single_tool(w->ctx, w->call, w->result, w->entry);
    return NULL;
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

    /* ================================================================
     * Phase 1: Pre-screen (serial) — dedup, subagent limits, approval
     * ================================================================ */
    PreScreenEntry* approved = calloc(call_count, sizeof(PreScreenEntry));
    if (approved == NULL) {
        *executed_count = 0;
        return -1;
    }
    int approved_count = 0;

    for (int i = 0; i < call_count; i++) {
        if (interrupt_pending()) {
            interrupt_acknowledge();
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
                display_streaming_tool_result(calls[i].id, calls[i].name,
                                              calls[i].arguments,
                                              "Duplicate subagent blocked", 0);
            } else {
                json_output_tool_result(calls[i].id, results[slot].result, 1);
            }
            count++;
            continue;
        } else if (strcmp(calls[i].name, "subagent") == 0) {
            debug_printf("First subagent call (ID: %s)\n", calls[i].id);
        }

        ApprovedPath out_path;
        init_approved_path(&out_path);
        int approval = tool_orchestration_check_approval(ctx->orchestration,
                                                          &calls[i], &results[slot],
                                                          &out_path);
        if (approval == -2) {
            free_approved_path(&out_path);
            status = -1;
            debug_printf("User aborted tool execution at tool %d of %d\n", i + 1, call_count);
            fill_error_result(&results[slot], calls[i].id,
                              "{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
            display_streaming_tool_result(calls[i].id, calls[i].name,
                                        calls[i].arguments, "Aborted by user", 0);
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
            free_approved_path(&out_path);
            debug_printf("Tool %s blocked by approval gate\n", calls[i].name);
            if (json_mode) {
                json_output_tool_result(calls[i].id, results[slot].result,
                                        !results[slot].success);
            }
            count++;
            continue;
        }

        PreScreenEntry* e = &approved[approved_count++];
        e->call_index = i;
        e->slot = slot;
        e->is_mcp = (strncmp(calls[i].name, "mcp_", 4) == 0);
        e->approved = 1;
        e->thread_safe = 0;
        for (size_t t = 0; t < ctx->session->tools.functions.count; t++) {
            if (strcmp(ctx->session->tools.functions.data[t].name, calls[i].name) == 0) {
                e->thread_safe = ctx->session->tools.functions.data[t].thread_safe;
                break;
            }
        }
        if (out_path.resolved_path != NULL) {
            e->approved_path = out_path;
            e->has_approved_path = 1;
        } else {
            free_approved_path(&out_path);
            e->has_approved_path = 0;
        }

        count++;
    }

    if (status != 0 || approved_count == 0) {
        for (int k = 0; k < approved_count; k++) {
            if (approved[k].has_approved_path) {
                free_approved_path(&approved[k].approved_path);
            }
        }
        free(approved);
        if (!json_mode && count > 0) {
            printf("\n");
            fflush(stdout);
        }
        *executed_count = count;
        return status;
    }

    /* ================================================================
     * Phase 2: Execute (parallel for 2+ thread-safe tools, else serial)
     * ================================================================ */
    int all_thread_safe = 1;
    for (int k = 0; k < approved_count; k++) {
        if (!approved[k].thread_safe) {
            all_thread_safe = 0;
            break;
        }
    }

    int use_parallel = (approved_count >= 2 && all_thread_safe);
    debug_printf("Batch: %d approved tools, all_thread_safe=%d, parallel=%d\n",
                 approved_count, all_thread_safe, use_parallel);

    if (approved_count == 1) {
        PreScreenEntry* e = &approved[0];
        spinner_start(calls[e->call_index].name, calls[e->call_index].arguments);
        execute_single_tool(ctx, &calls[e->call_index], &results[e->slot], e);
        spinner_stop();
    } else if (!use_parallel) {
        for (int k = 0; k < approved_count; k++) {
            PreScreenEntry* e = &approved[k];
            spinner_start(calls[e->call_index].name, calls[e->call_index].arguments);
            execute_single_tool(ctx, &calls[e->call_index], &results[e->slot], e);
            spinner_stop();
        }
    } else {
        char label[32];
        snprintf(label, sizeof(label), "%d tools", approved_count);
        spinner_start(label, NULL);

        pthread_t* threads = malloc(approved_count * sizeof(pthread_t));
        WorkerArg* args = malloc(approved_count * sizeof(WorkerArg));
        int* created = calloc(approved_count, sizeof(int));
        if (threads == NULL || args == NULL || created == NULL) {
            free(threads);
            free(args);
            free(created);
            for (int k = 0; k < approved_count; k++) {
                PreScreenEntry* e = &approved[k];
                execute_single_tool(ctx, &calls[e->call_index], &results[e->slot], e);
            }
        } else {
            for (int k = 0; k < approved_count; k++) {
                PreScreenEntry* e = &approved[k];
                args[k] = (WorkerArg){
                    .ctx = ctx,
                    .call = &calls[e->call_index],
                    .result = &results[e->slot],
                    .entry = e
                };
                if (pthread_create(&threads[k], NULL, worker_thread, &args[k]) != 0) {
                    debug_printf("Failed to create thread for tool %s, executing inline\n",
                               calls[e->call_index].name);
                    execute_single_tool(ctx, &calls[e->call_index], &results[e->slot], e);
                } else {
                    created[k] = 1;
                }
            }

            for (int k = 0; k < approved_count; k++) {
                if (created[k]) {
                    pthread_join(threads[k], NULL);
                }
            }
            free(threads);
            free(args);
            free(created);
        }

        spinner_stop();
    }

    if (interrupt_pending()) {
        interrupt_acknowledge();
        status = -2;
    }

    /* ================================================================
     * Phase 3: Post-process (serial) — log results in original order
     * ================================================================ */
    for (int k = 0; k < approved_count; k++) {
        PreScreenEntry* e = &approved[k];
        int i = e->call_index;
        int slot = e->slot;

        if (e->has_approved_path) {
            free_approved_path(&e->approved_path);
        }

        if (!json_mode) {
            display_streaming_tool_result(calls[i].id, calls[i].name,
                                          calls[i].arguments,
                                          results[slot].result, results[slot].success);

            if (strcmp(calls[i].name, "subagent") == 0 && results[slot].success) {
                cJSON *args_json = cJSON_Parse(calls[i].arguments);
                if (args_json) {
                    cJSON *task_item = cJSON_GetObjectItem(args_json, "task");
                    const char *task_str = (task_item && cJSON_IsString(task_item))
                                           ? task_item->valuestring : "subagent";
                    display_agents_launched(1, &task_str);
                    cJSON_Delete(args_json);
                }
            }
        }

        if (json_mode) {
            json_output_tool_result(calls[i].id, results[slot].result,
                                    !results[slot].success);
        }
    }

    free(approved);

    if (!json_mode && count > 0) {
        printf("\n");
        fflush(stdout);
    }

    *executed_count = count;
    return status;
}
