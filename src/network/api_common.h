#ifndef API_COMMON_H
#define API_COMMON_H

#include "ralph.h"
#include "conversation_tracker.h"
#include "tools_system.h"

size_t calculate_json_payload_size(const char* model, const char* system_prompt,
                                  const ConversationHistory* conversation,
                                  const char* user_message, const ToolRegistry* tools);

typedef int (*MessageFormatter)(char* buffer, size_t buffer_size,
                               const ConversationMessage* message,
                               int is_first_message);

int format_openai_message(char* buffer, size_t buffer_size,
                         const ConversationMessage* message,
                         int is_first_message);

int format_anthropic_message(char* buffer, size_t buffer_size,
                            const ConversationMessage* message,
                            int is_first_message);

int build_messages_json(char* buffer, size_t buffer_size,
                       const char* system_prompt,
                       const ConversationHistory* conversation,
                       const char* user_message,
                       MessageFormatter formatter,
                       int skip_system_in_history);

int build_anthropic_messages_json(char* buffer, size_t buffer_size,
                                 const char* system_prompt,
                                 const ConversationHistory* conversation,
                                 const char* user_message,
                                 MessageFormatter formatter,
                                 int skip_system_in_history);

char* build_json_payload_common(const char* model, const char* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message, const char* max_tokens_param,
                               int max_tokens, const ToolRegistry* tools,
                               MessageFormatter formatter,
                               int system_at_top_level);

char* build_json_payload_model_aware(const char* model, const char* system_prompt,
                                    const ConversationHistory* conversation,
                                    const char* user_message, const char* max_tokens_param,
                                    int max_tokens, const ToolRegistry* tools,
                                    MessageFormatter formatter,
                                    int system_at_top_level);

#endif // API_COMMON_H