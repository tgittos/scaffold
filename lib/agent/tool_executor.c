#define LOG_MODULE     LOG_MOD_AGENT
#define LOG_MODULE_STR "agent"
#include "../util/log.h"

#include "tool_executor.h"
#include "session.h"
#include "tool_orchestration.h"
#include "tool_batch_executor.h"
#include "conversation_state.h"
#include "iterative_loop.h"
#include "../session/conversation_tracker.h"
#include <stdlib.h>
#include <string.h>

int tool_executor_run_workflow(AgentSession* session, ToolCall* tool_calls, int call_count,
                               const char* user_message, int max_tokens) {
    if (session == NULL || tool_calls == NULL || call_count <= 0) {
        return -1;
    }

    (void)user_message;
    (void)max_tokens;

    LOG_INFO("Executing %d tool call(s)", call_count);

    ToolOrchestrationContext ctx;
    if (tool_orchestration_init(&ctx, &session->gate_config) != 0) {
        return -1;
    }

    ToolResult *results = calloc(call_count, sizeof(ToolResult));
    if (results == NULL) {
        tool_orchestration_cleanup(&ctx);
        return -1;
    }

    ToolBatchContext batch_ctx = { .session = session, .orchestration = &ctx };
    int executed_count = 0;
    int batch_status = tool_batch_execute(&batch_ctx, tool_calls, call_count,
                                           results, NULL, &executed_count);

    /* Check if any tool requested a conversation reset (e.g. execute_plan
     * clears planning context before decomposition begins). */
    for (int i = 0; i < call_count; i++) {
        if (results[i].clear_history) {
            LOG_INFO("Tool %s requested conversation clear",
                     tool_calls[i].name ? tool_calls[i].name : "?");
            cleanup_conversation_history(&session->session_data.conversation);
            init_conversation_history(&session->session_data.conversation);
            /* Re-append the assistant message with tool calls so the
             * conversation has the required assistant→tool_result structure. */
            conversation_append_assistant(session, NULL, tool_calls, call_count);
            break;
        }
    }

    conversation_append_tool_results(session, results, call_count, tool_calls, NULL);

    if (batch_status != 0) {
        cleanup_tool_results(results, call_count);
        tool_orchestration_cleanup(&ctx);
        return -2;
    }

    // Seed the tracker with IDs from the initial batch so the iterative loop
    // can detect re-emitted IDs and avoid duplicate execution.
    for (int i = 0; i < call_count; i++) {
        tool_orchestration_mark_executed(&ctx, tool_calls[i].id);
    }

    /* Track workflow state from initial tool batch */
    LoopWorkflowState wf_state = {0};
    wf_state.has_used_tools = 1;  /* We're executing tools right now */
    for (int i = 0; i < call_count; i++) {
        if (tool_calls[i].name == NULL) continue;
        if (strcmp(tool_calls[i].name, "apply_patch") == 0 ||
            strcmp(tool_calls[i].name, "write_file") == 0) {
            wf_state.has_patched = 1;
            wf_state.has_tested_since_patch = 0;
        } else if (strcmp(tool_calls[i].name, "shell") == 0) {
            wf_state.has_tested_since_patch = 1;
        }
    }

    int result = iterative_loop_run(session, &ctx, &wf_state);

    // Context full is a special signal that must propagate to the supervisor
    if (result == SESSION_CONTEXT_FULL) {
        LOG_WARN("tool_executor: context full, propagating");
        cleanup_tool_results(results, call_count);
        tool_orchestration_cleanup(&ctx);
        return SESSION_CONTEXT_FULL;
    }

    /* Initial tools are already in conversation history; retrying would corrupt state */
    if (result != 0) {
        LOG_WARN("Follow-up tool loop failed, but initial tools executed successfully");
        result = 0;
    }

    cleanup_tool_results(results, call_count);
    tool_orchestration_cleanup(&ctx);
    return result;
}
