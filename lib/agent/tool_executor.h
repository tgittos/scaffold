#ifndef TOOL_EXECUTOR_H
#define TOOL_EXECUTOR_H

#include "session.h"
#include "../tools/tools_system.h"

/**
 * Tool Executor — Entry Point
 *
 * Thin orchestrator that wires together the tool execution pipeline:
 * 1. Initializes the orchestration context (approval + dedup)
 * 2. Executes the initial batch via tool_batch_executor
 * 3. Hands off to iterative_loop for follow-up LLM rounds
 */

/**
 * Execute tool calls and handle iterative follow-up calls.
 *
 * This is the main entry point for tool execution workflow. It executes
 * the provided tool calls, saves results to conversation history, and
 * continues making API calls until no more tool calls are returned.
 *
 * Before execution, each tool call is checked against approval gates:
 * - Protected files are blocked unconditionally
 * - Rate-limited tools return error without prompting
 * - Gated tools prompt for user approval (if interactive)
 * - Allowed tools proceed without prompting
 *
 * @param session The Ralph session containing config, conversation, and tools
 * @param tool_calls Array of tool calls to execute
 * @param call_count Number of tool calls in the array
 * @param user_message The original user message (for context in follow-ups)
 * @param max_tokens Maximum tokens for response generation
 * @return 0 on success, -1 on failure, -2 if user aborted (Ctrl+C)
 */
int tool_executor_run_workflow(AgentSession* session,
                               ToolCall* tool_calls,
                               int call_count,
                               const char* user_message,
                               int max_tokens);

#endif // TOOL_EXECUTOR_H
