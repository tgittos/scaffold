#ifndef TOOL_REVIEW_H
#define TOOL_REVIEW_H

#include "../types.h"

/**
 * Auto mode's model review of a gated tool call, sent to the session's own
 * model as a one-off request that never enters conversation history. The
 * reviewer sees the user's messages and the call, never tool output.
 *
 * Matches ApprovalGateCallbacks.review_tool_call.
 *
 * @param session AgentSession* whose model and conversation to use
 * @param tool_call The gated call
 * @param out_reason Output: allocated explanation, or NULL; caller frees
 * @return 1 when the reviewer allows the call, 0 when the user must decide
 */
int tool_review_run(void *session, const ToolCall *tool_call, char **out_reason);

#endif /* TOOL_REVIEW_H */
