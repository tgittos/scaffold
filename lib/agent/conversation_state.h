#ifndef LIB_AGENT_CONVERSATION_STATE_H
#define LIB_AGENT_CONVERSATION_STATE_H

#include "session.h"
#include "../types.h"

/**
 * Append an assistant message to the conversation history.
 *
 * When tool calls are present (count > 0), the message is formatted using
 * the model registry's format_model_assistant_tool_message with a fallback
 * to a simple tool-name summary. When count == 0, the content is appended
 * as a plain assistant message.
 *
 * @param session  The agent session
 * @param content  Assistant text content (may be NULL)
 * @param calls    Tool calls array (may be NULL when count == 0)
 * @param count    Number of tool calls
 * @return 0 on success, -1 on failure
 */
int conversation_append_assistant(AgentSession* session,
                                  const char* content,
                                  const ToolCall* calls,
                                  int count);

/**
 * Append tool results to the conversation history.
 *
 * Each result is paired with its corresponding tool call to obtain the
 * tool name required by the conversation tracker.
 *
 * When call_indices is NULL, results[i] maps to source_calls[i] directly.
 * When call_indices is provided, results[i] maps to
 * source_calls[call_indices[i]].
 *
 * @param session       The agent session
 * @param results       Array of tool results
 * @param result_count  Number of results to append
 * @param source_calls  Tool calls array (provides tool names)
 * @param call_indices  Optional index mapping, or NULL for 1:1
 * @return 0 on success, -1 on first append failure
 */
int conversation_append_tool_results(AgentSession* session,
                                     const ToolResult* results,
                                     int result_count,
                                     const ToolCall* source_calls,
                                     const int* call_indices);

#endif
