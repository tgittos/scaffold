#ifndef STREAMING_HANDLER_H
#define STREAMING_HANDLER_H

#include "session.h"
#include "../llm/llm_provider.h"

/**
 * Streaming Handler Module
 *
 * This module provides the application-layer streaming orchestration for ralph.
 * It connects streaming infrastructure to the display system and handles the
 * complete streaming message flow including tool execution.
 *
 * The low-level SSE parsing is handled by lib/network/streaming.c
 */

/**
 * Process a message using streaming API.
 *
 * The caller supplies the detected LLMProvider to avoid redundant provider
 * detection (message_dispatcher_select_mode already resolved it).
 *
 * @param session The ralph session
 * @param provider The detected LLM provider (must support streaming)
 * @param user_message The user's input message
 * @param max_tokens Maximum tokens for the response
 * @return 0 on success, -1 on error
 */
int streaming_process_message(AgentSession* session, LLMProvider* provider,
                              const char* user_message, int max_tokens);

#endif /* STREAMING_HANDLER_H */
