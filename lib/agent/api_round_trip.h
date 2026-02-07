#ifndef LIB_AGENT_API_ROUND_TRIP_H
#define LIB_AGENT_API_ROUND_TRIP_H

#include "session.h"
#include "../types.h"
#include "../ui/output_formatter.h"

typedef struct {
    ParsedResponse parsed;
    ToolCall* tool_calls;
    int tool_call_count;
} LLMRoundTripResult;

int api_round_trip_execute(AgentSession* session, const char* user_message,
                           int max_tokens, LLMRoundTripResult* result);

void api_round_trip_cleanup(LLMRoundTripResult* result);

#endif
