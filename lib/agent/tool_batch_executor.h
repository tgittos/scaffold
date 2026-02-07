#ifndef LIB_AGENT_TOOL_BATCH_EXECUTOR_H
#define LIB_AGENT_TOOL_BATCH_EXECUTOR_H

#include "session.h"
#include "tool_orchestration.h"
#include "../types.h"

/**
 * Tool Batch Executor
 *
 * Executes a batch of tool calls with approval gates, dedup, and interrupt
 * handling. Supports two indexing modes:
 *
 * Direct mode (call_indices == NULL):
 *   results[i] maps to calls[i]. All slots are filled even on abort/interrupt.
 *   No deduplication. *executed_count is always set to call_count.
 *
 * Compact mode (call_indices != NULL):
 *   Deduplication via orchestration context. Only non-duplicate calls produce
 *   results. results[k] maps to calls[call_indices[k]]. On abort, only the
 *   aborting tool is added. On interrupt, remaining non-duplicates get results.
 */

typedef struct {
    AgentSession* session;
    ToolOrchestrationContext* orchestration;
} ToolBatchContext;

/**
 * Execute a batch of tool calls.
 *
 * @param ctx             Batch context (session + orchestration)
 * @param calls           Tool calls to execute
 * @param call_count      Number of tool calls
 * @param results         Pre-allocated result array (at least call_count elements)
 * @param call_indices    Index mapping array, or NULL for direct 1:1 mode
 * @param executed_count  Output: number of results populated
 * @return 0 on success, -1 on abort, -2 on interrupt
 */
int tool_batch_execute(ToolBatchContext* ctx,
                       ToolCall* calls,
                       int call_count,
                       ToolResult* results,
                       int* call_indices,
                       int* executed_count);

#endif /* LIB_AGENT_TOOL_BATCH_EXECUTOR_H */
