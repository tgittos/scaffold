#ifndef LIB_AGENT_MESSAGE_DISPATCHER_H
#define LIB_AGENT_MESSAGE_DISPATCHER_H

#include "session.h"
#include "../llm/llm_provider.h"

typedef enum {
    DISPATCH_STREAMING,
    DISPATCH_BUFFERED
} DispatchMode;

typedef struct {
    DispatchMode mode;
    LLMProvider* provider;
} DispatchDecision;

DispatchDecision message_dispatcher_select_mode(const AgentSession* session);

char* message_dispatcher_build_payload(const AgentSession* session,
                                       const char* user_message,
                                       int max_tokens);

#endif /* LIB_AGENT_MESSAGE_DISPATCHER_H */
