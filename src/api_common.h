#ifndef API_COMMON_H
#define API_COMMON_H

#include "ralph.h"
#include "conversation_tracker.h"
#include "tools_system.h"

// Common API message formatting functions

/**
 * Calculate buffer size required for JSON payload
 */
size_t calculate_json_payload_size(const char* model, const char* system_prompt,
                                  const ConversationHistory* conversation,
                                  const char* user_message, const ToolRegistry* tools);

/**
 * Format a single conversation message as JSON
 * Returns the number of characters written
 */
typedef int (*MessageFormatter)(char* buffer, size_t buffer_size, 
                               const ConversationMessage* message,
                               int is_first_message);

/**
 * Standard OpenAI-style message formatter
 */
int format_openai_message(char* buffer, size_t buffer_size,
                         const ConversationMessage* message,
                         int is_first_message);

/**
 * Anthropic-style message formatter
 */
int format_anthropic_message(char* buffer, size_t buffer_size,
                            const ConversationMessage* message,
                            int is_first_message);

/**
 * Build the messages array portion of JSON payload
 */
int build_messages_json(char* buffer, size_t buffer_size,
                       const char* system_prompt,
                       const ConversationHistory* conversation,
                       const char* user_message,
                       MessageFormatter formatter,
                       int skip_system_in_history);

/**
 * Anthropic-specific messages builder that validates tool_result/tool_use pairing
 */
int build_anthropic_messages_json(char* buffer, size_t buffer_size,
                                 const char* system_prompt,
                                 const ConversationHistory* conversation,
                                 const char* user_message,
                                 MessageFormatter formatter,
                                 int skip_system_in_history);

/**
 * API-specific JSON builders that use common components
 */
char* build_json_payload_common(const char* model, const char* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message, const char* max_tokens_param,
                               int max_tokens, const ToolRegistry* tools,
                               MessageFormatter formatter,
                               int system_at_top_level);

#endif // API_COMMON_H