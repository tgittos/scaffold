#ifndef LIB_AGENT_MESSAGE_PROCESSOR_H
#define LIB_AGENT_MESSAGE_PROCESSOR_H

#include "session.h"
#include "api_round_trip.h"

/**
 * Handle a buffered LLM response: persist conversation history,
 * execute tool calls, or output plain text.
 *
 * Takes ownership of tool calls from the round-trip result (sets them
 * to NULL). The caller must still call api_round_trip_cleanup to free
 * the ParsedResponse fields.
 *
 * @param session       The agent session
 * @param result        Round-trip result from api_round_trip_execute
 * @param user_message  The original user message (saved to history)
 * @param max_tokens    Max tokens for tool continuation responses
 * @return 0 on success, -1 on error, -2 if user aborted
 */
int message_processor_handle_response(AgentSession* session,
                                      LLMRoundTripResult* result,
                                      const char* user_message,
                                      int max_tokens);

#endif /* LIB_AGENT_MESSAGE_PROCESSOR_H */
