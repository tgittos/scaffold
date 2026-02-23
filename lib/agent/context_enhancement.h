#ifndef CONTEXT_ENHANCEMENT_H
#define CONTEXT_ENHANCEMENT_H

#include "session.h"

/* Split prompt for cache-friendly API requests.
 * base_prompt is the session-stable system prompt (strdup'd).
 * dynamic_context is per-request additions: todo, mode, summary, memories, context. */
typedef struct {
    char* base_prompt;
    char* dynamic_context;
} EnhancedPromptParts;

void free_enhanced_prompt_parts(EnhancedPromptParts* parts);

/**
 * Build a split prompt with todo state, memory recall, and context retrieval.
 * The caller must call free_enhanced_prompt_parts() on the result.
 *
 * @param session The agent session
 * @param user_message The current user message (used for memory/context retrieval)
 * @param out Output struct to populate
 * @return 0 on success, -1 on failure
 */
int build_enhanced_prompt_parts(const AgentSession* session,
                                const char* user_message,
                                EnhancedPromptParts* out);

#endif // CONTEXT_ENHANCEMENT_H
