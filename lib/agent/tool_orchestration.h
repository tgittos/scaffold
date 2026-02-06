#ifndef LIB_AGENT_TOOL_ORCHESTRATION_H
#define LIB_AGENT_TOOL_ORCHESTRATION_H

#include "../policy/approval_gate.h"
#include "../types.h"
#include "../util/ptrarray.h"

/**
 * Tool Orchestration Module
 *
 * Manages cross-cutting concerns for tool execution batches:
 * - Approval gate checking (protected files, user consent)
 * - Tool call deduplication across loop iterations
 * - Subagent spawn limiting (one per batch)
 */

typedef struct {
    ApprovalGateConfig* gate_config;
    StringArray executed_tracker;
    int subagent_spawned;
} ToolOrchestrationContext;

int tool_orchestration_init(ToolOrchestrationContext* ctx,
                            ApprovalGateConfig* gate_config);
void tool_orchestration_cleanup(ToolOrchestrationContext* ctx);

/**
 * Check approval gates and protected files before tool execution.
 * Returns 0 to allow, -1 if blocked (result populated with error), -2 if user aborted.
 */
int tool_orchestration_check_approval(ToolOrchestrationContext* ctx,
                                      const ToolCall* call,
                                      ToolResult* result);

/**
 * Returns 1 if the tool call ID has already been executed, 0 if not.
 */
int tool_orchestration_is_duplicate(ToolOrchestrationContext* ctx,
                                    const char* tool_call_id);

/**
 * Record a tool call ID as executed.
 * Returns 0 on success, -1 on failure.
 */
int tool_orchestration_mark_executed(ToolOrchestrationContext* ctx,
                                     const char* tool_call_id);

/**
 * Check if a subagent can be spawned in this batch.
 * Returns 1 if allowed (and marks as spawned), 0 if blocked (duplicate).
 * Returns 1 for non-subagent tools.
 */
int tool_orchestration_can_spawn_subagent(ToolOrchestrationContext* ctx,
                                          const char* tool_name);

/**
 * Reset per-batch state (subagent_spawned flag).
 * Call at the start of each iteration in the tool loop.
 */
void tool_orchestration_reset_batch(ToolOrchestrationContext* ctx);

#endif /* LIB_AGENT_TOOL_ORCHESTRATION_H */
