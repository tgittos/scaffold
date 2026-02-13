#include "tool_executor.h"
#include "session.h"
#include "tool_orchestration.h"
#include "tool_batch_executor.h"
#include "conversation_state.h"
#include "iterative_loop.h"
#include "../session/conversation_tracker.h"
#include "../util/debug_output.h"
#include <stdlib.h>

int tool_executor_run_workflow(AgentSession* session, ToolCall* tool_calls, int call_count,
                               const char* user_message, int max_tokens) {
    if (session == NULL || tool_calls == NULL || call_count <= 0) {
        return -1;
    }

    (void)user_message;  /* reserved for future context-aware tool execution */
    (void)max_tokens;    /* reserved for future token-budget-aware execution */

    debug_printf("Executing %d tool call(s)...\n", call_count);

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
            debug_printf("Tool %s requested conversation clear\n",
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

    int result = iterative_loop_run(session, &ctx);

    // Follow-up loop failure is non-fatal since initial tools already executed
    if (result != 0) {
        debug_printf("Follow-up tool loop failed, but initial tools executed successfully\n");
        result = 0;
    }

    cleanup_tool_results(results, call_count);
    tool_orchestration_cleanup(&ctx);
    return result;
}
