#ifndef API_COMMON_H
#define API_COMMON_H

#include "image_attachment.h"

/* Forward declarations to break cross-layer header dependencies */
typedef struct ConversationHistory ConversationHistory;
typedef struct ConversationMessage ConversationMessage;
typedef struct ToolRegistry ToolRegistry;

/* Split system prompt for cache-friendly API requests.
 * base_prompt stays identical across requests in a session (cacheable prefix).
 * dynamic_context changes per-request (todo state, mode, memories, context). */
typedef struct {
    const char* base_prompt;
    const char* dynamic_context;
} SystemPromptParts;

char* summarize_tool_calls(const char* raw_json);

/* Flags for streaming_add_params */
#define STREAM_INCLUDE_USAGE (1 << 0)
#define STREAM_NO_STORE      (1 << 1)

/* Add streaming parameters to a cJSON root object.
 * flags: bitmask of STREAM_INCLUDE_USAGE, STREAM_NO_STORE */
struct cJSON;
void streaming_add_params(struct cJSON *root, int flags);

void api_common_set_pending_images(const ImageAttachment *images, size_t count);
void api_common_clear_pending_images(void);

size_t calculate_messages_buffer_size(const SystemPromptParts* system_prompt,
                                     const ConversationHistory* conversation,
                                     const char* user_message);

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
                       const SystemPromptParts* system_prompt,
                       const ConversationHistory* conversation,
                       const char* user_message,
                       MessageFormatter formatter,
                       int skip_system_in_history);

char* build_json_payload_common(const char* model, const SystemPromptParts* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message, const char* max_tokens_param,
                               int max_tokens, const ToolRegistry* tools,
                               MessageFormatter formatter,
                               int system_at_top_level);

char* build_json_payload_model_aware(const char* model, const SystemPromptParts* system_prompt,
                                    const ConversationHistory* conversation,
                                    const char* user_message, const char* max_tokens_param,
                                    int max_tokens, const ToolRegistry* tools,
                                    MessageFormatter formatter,
                                    int system_at_top_level);

#endif // API_COMMON_H
