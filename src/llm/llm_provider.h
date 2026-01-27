#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "conversation_tracker.h"
#include "tools_system.h"
#include "http_client.h"
#include "output_formatter.h"
#include "streaming.h"
#include "utils/ptrarray.h"

struct LLMProvider;

typedef struct {
    const char* name;
    const char* max_tokens_param;
    int supports_system_message;
    int requires_version_header;
    const char* auth_header_format;  /* e.g. "Authorization: Bearer %s" or "x-api-key: %s" */
    const char* version_header;      /* NULL if provider doesn't require one */
} ProviderCapabilities;

typedef struct LLMProvider {
    ProviderCapabilities capabilities;

    int (*detect_provider)(const char* api_url);

    char* (*build_request_json)(const struct LLMProvider* provider,
                               const char* model,
                               const char* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message,
                               int max_tokens,
                               const ToolRegistry* tools);

    int (*build_headers)(const struct LLMProvider* provider,
                        const char* api_key,
                        const char** headers,
                        int max_headers);

    int (*parse_response)(const struct LLMProvider* provider,
                         const char* json_response,
                         ParsedResponse* result);

    /* Tool calling is handled through ModelCapabilities, not here. */

    int (*supports_streaming)(const struct LLMProvider* provider);

    int (*parse_stream_event)(const struct LLMProvider* provider,
                             StreamingContext* ctx,
                             const char* json_data,
                             size_t len);

    char* (*build_streaming_request_json)(const struct LLMProvider* provider,
                                          const char* model,
                                          const char* system_prompt,
                                          const ConversationHistory* conversation,
                                          const char* user_message,
                                          int max_tokens,
                                          const ToolRegistry* tools);

    void (*cleanup_stream_state)(const struct LLMProvider* provider);

} LLMProvider;

PTRARRAY_DECLARE(ProviderRegistry, LLMProvider)

int init_provider_registry(ProviderRegistry* registry);
int register_provider(ProviderRegistry* registry, LLMProvider* provider);
LLMProvider* detect_provider_for_url(ProviderRegistry* registry, const char* api_url);
void cleanup_provider_registry(ProviderRegistry* registry);

int register_openai_provider(ProviderRegistry* registry);
int register_anthropic_provider(ProviderRegistry* registry);
int register_local_ai_provider(ProviderRegistry* registry);

#endif // LLM_PROVIDER_H