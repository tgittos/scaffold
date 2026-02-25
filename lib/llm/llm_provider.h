#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "../network/http_client.h"
#include "../network/streaming.h"
#include "../network/api_common.h"
#include "../util/ptrarray.h"

#define CODEX_URL_PATTERN    "chatgpt.com/backend-api/codex"
#define MAX_AUTH_HEADER_SIZE 2200

/* Forward declarations to break cross-layer header dependencies */
typedef struct ConversationHistory ConversationHistory;
typedef struct ToolRegistry ToolRegistry;
typedef struct ParsedResponse ParsedResponse;

struct LLMProvider;

typedef struct {
    const char* name;
    const char* max_tokens_param;
    int supports_system_message;
} ProviderCapabilities;

typedef struct LLMProvider {
    ProviderCapabilities capabilities;

    int (*detect_provider)(const char* api_url);

    char* (*build_request_json)(const struct LLMProvider* provider,
                               const char* model,
                               const SystemPromptParts* system_prompt,
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
                                          const SystemPromptParts* system_prompt,
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

int register_codex_provider(ProviderRegistry* registry);
int register_openai_provider(ProviderRegistry* registry);
int register_anthropic_provider(ProviderRegistry* registry);
int register_local_ai_provider(ProviderRegistry* registry);

ProviderRegistry* get_provider_registry(void);
void provider_registry_cleanup(void);

#endif // LLM_PROVIDER_H
