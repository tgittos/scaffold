#ifndef CONTEXT_ENHANCEMENT_H
#define CONTEXT_ENHANCEMENT_H

#include "ralph.h"

/**
 * Build a complete prompt with todo state, memory recall, and context retrieval.
 * This is the main entry point for prompt enhancement before API calls.
 * The caller must free the returned string.
 *
 * @param session The Ralph session
 * @param user_message The current user message (used for memory/context retrieval)
 * @return Allocated string with fully enhanced prompt, or NULL on failure
 */
char* build_enhanced_prompt_with_context(const RalphSession* session,
                                         const char* user_message);

#endif // CONTEXT_ENHANCEMENT_H
