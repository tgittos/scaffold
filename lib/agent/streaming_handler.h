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
 * Process a message using streaming API
 *
 * This function handles the complete streaming message flow:
 * 1. Builds streaming request JSON using the provider
 * 2. Sets up display callbacks for real-time output
 * 3. Executes the streaming HTTP request
 * 4. Handles tool calls if any are returned
 * 5. Saves messages to conversation history
 *
 * @param session The ralph session
 * @param user_message The user's input message
 * @param max_tokens Maximum tokens for the response
 * @return 0 on success, -1 on error
 */
int streaming_process_message(AgentSession* session, const char* user_message,
                              int max_tokens);

#endif /* STREAMING_HANDLER_H */
