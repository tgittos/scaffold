#ifndef STREAMING_HANDLER_H
#define STREAMING_HANDLER_H

#include "ralph.h"
#include "lib/llm/llm_provider.h"

/**
 * Streaming Handler Module
 *
 * This module provides the application-layer streaming orchestration for ralph.
 * It manages the provider registry, connects streaming infrastructure to the
 * display system, and handles the complete streaming message flow including
 * tool execution.
 *
 * The low-level SSE parsing is handled by lib/network/streaming.c
 */

/**
 * Get or create the global provider registry
 *
 * Lazily initializes the provider registry with all built-in providers
 * (OpenAI, Anthropic, LocalAI) on first call.
 *
 * NOTE: This function is NOT thread-safe. It assumes single-threaded usage
 * which is the case for ralph's CLI architecture.
 *
 * @return Pointer to the provider registry, or NULL on allocation failure
 */
ProviderRegistry* streaming_get_provider_registry(void);

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
 * @param headers HTTP headers for authentication
 * @return 0 on success, -1 on error
 */
int streaming_process_message(RalphSession* session, const char* user_message,
                              int max_tokens, const char** headers);

/**
 * Cleanup streaming handler resources
 *
 * Frees the global provider registry. Should be called from
 * ralph_cleanup_session() during shutdown.
 */
void streaming_handler_cleanup(void);

#endif /* STREAMING_HANDLER_H */
