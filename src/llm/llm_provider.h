#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include "conversation_tracker.h"
#include "tools_system.h"
#include "http_client.h"
#include "output_formatter.h"
#include "streaming.h"
#include "utils/ptrarray.h"

// Forward declarations
struct LLMProvider;

// Provider capabilities and configuration
typedef struct {
    const char* name;
    const char* max_tokens_param;
    int supports_system_message;
    int requires_version_header;
    const char* auth_header_format;  // "Authorization: Bearer %s" or "x-api-key: %s"
    const char* version_header;      // Optional version header
} ProviderCapabilities;

// Provider interface - all providers must implement these functions
typedef struct LLMProvider {
    ProviderCapabilities capabilities;

    // Provider detection
    int (*detect_provider)(const char* api_url);

    // Request building
    char* (*build_request_json)(const struct LLMProvider* provider,
                               const char* model,
                               const char* system_prompt,
                               const ConversationHistory* conversation,
                               const char* user_message,
                               int max_tokens,
                               const ToolRegistry* tools);

    // HTTP headers
    int (*build_headers)(const struct LLMProvider* provider,
                        const char* api_key,
                        const char** headers,
                        int max_headers);

    // Response parsing
    int (*parse_response)(const struct LLMProvider* provider,
                         const char* json_response,
                         ParsedResponse* result);

    // Message formatting for conversation history
    // Note: Tool calling is now handled through ModelCapabilities

    // Streaming support
    int (*supports_streaming)(const struct LLMProvider* provider);

    // Parse a single SSE data line from streaming response
    int (*parse_stream_event)(const struct LLMProvider* provider,
                             StreamingContext* ctx,
                             const char* json_data,
                             size_t len);

    // Build streaming request JSON (adds stream: true and stream_options)
    char* (*build_streaming_request_json)(const struct LLMProvider* provider,
                                          const char* model,
                                          const char* system_prompt,
                                          const ConversationHistory* conversation,
                                          const char* user_message,
                                          int max_tokens,
                                          const ToolRegistry* tools);

    // Clean up any provider-specific streaming state (called on stream end or error)
    void (*cleanup_stream_state)(const struct LLMProvider* provider);

} LLMProvider;

// Provider registry and factory
PTRARRAY_DECLARE(ProviderRegistry, LLMProvider)

// Core provider management functions
int init_provider_registry(ProviderRegistry* registry);
int register_provider(ProviderRegistry* registry, LLMProvider* provider);
LLMProvider* detect_provider_for_url(ProviderRegistry* registry, const char* api_url);
void cleanup_provider_registry(ProviderRegistry* registry);

// Built-in provider registration functions
int register_openai_provider(ProviderRegistry* registry);
int register_anthropic_provider(ProviderRegistry* registry);
int register_local_ai_provider(ProviderRegistry* registry);

#endif // LLM_PROVIDER_H