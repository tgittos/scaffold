#ifndef LIB_AGENT_MESSAGE_DISPATCHER_H
#define LIB_AGENT_MESSAGE_DISPATCHER_H

#include "session.h"
#include "context_enhancement.h"
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

/**
 * Build enhanced prompt parts with plugin hook dispatch.
 * Calls build_enhanced_prompt_parts, then dispatches context_enhance
 * and pre_llm_send hooks. Shared by streaming and buffered paths.
 *
 * @param session Agent session
 * @param user_message Current user message
 * @param parts Output struct (caller must call free_enhanced_prompt_parts)
 * @return 0 on success, -1 on failure
 */
int message_dispatcher_prepare_prompt(AgentSession* session,
                                      const char* user_message,
                                      EnhancedPromptParts* parts);

/**
 * Build the complete JSON payload for a buffered LLM request.
 * Calls message_dispatcher_prepare_prompt internally — callers must NOT
 * call prepare_prompt separately or hooks will fire twice.
 */
char* message_dispatcher_build_payload(AgentSession* session,
                                       const char* user_message,
                                       int max_tokens);

#endif /* LIB_AGENT_MESSAGE_DISPATCHER_H */
