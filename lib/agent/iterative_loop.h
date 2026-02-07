#ifndef LIB_AGENT_ITERATIVE_LOOP_H
#define LIB_AGENT_ITERATIVE_LOOP_H

#include "session.h"
#include "tool_orchestration.h"

#define ITERATIVE_LOOP_MAX_ITERATIONS 200

/**
 * Run the iterative tool-calling loop.
 *
 * After the initial batch of tool calls has been executed, this function
 * continues the agentic loop: it makes follow-up API calls, executes
 * any requested tool calls, and repeats until the LLM returns a response
 * with no tool calls.
 *
 * Each iteration manages its own token budget via the token manager,
 * deduplicates tool calls via the orchestration context, and appends
 * results to the conversation history.
 *
 * @param session  The agent session
 * @param ctx      Orchestration context (tracks executed tool IDs)
 * @return 0 on success, -1 on error, -2 on interrupt
 */
int iterative_loop_run(AgentSession* session, ToolOrchestrationContext* ctx);

#endif /* LIB_AGENT_ITERATIVE_LOOP_H */
