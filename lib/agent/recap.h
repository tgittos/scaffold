#ifndef RECAP_H
#define RECAP_H

#include "session.h"

/**
 * Recap Generation Module
 *
 * This module provides conversation recap functionality for ralph.
 * It generates a brief summary of recent conversation history, or a
 * first-run greeting, using a one-shot LLM call that does NOT persist
 * to conversation history.
 *
 * This is useful for:
 * - Resuming conversations after a break
 * - Getting a quick summary of what was discussed
 * - Orienting the user when returning to a long conversation
 */

/**
 * Generate a recap of recent conversation without persisting to history.
 *
 * Makes a one-shot LLM call to summarize recent messages. The recap
 * prompt and response are NOT saved to conversation history to keep
 * it clean and avoid bloating with meta-conversation.
 *
 * @param session The agent session containing config and conversation
 * @param max_messages Maximum number of recent messages to include in recap
 *                     context (0 or negative for default of 5)
 * @return 0 on success, -1 on failure
 */
int recap_generate(AgentSession* session, int max_messages);

/**
 * Stream a first-run welcome message without persisting to history.
 *
 * The greeting prompt and response are NOT saved to conversation history,
 * so a later session with no real exchanges greets again instead of
 * recapping the greeting.
 *
 * @param session The agent session containing config
 * @param name Name the assistant introduces itself as
 * @return 0 on success, -1 on failure
 */
int recap_greet(AgentSession* session, const char* name);

#endif // RECAP_H
