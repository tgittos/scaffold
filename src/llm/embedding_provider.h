#ifndef EMBEDDING_PROVIDER_H
#define EMBEDDING_PROVIDER_H

#include "embeddings.h"
#include "utils/ptrarray.h"

// Forward declarations
struct EmbeddingProvider;

// Provider capabilities and configuration
typedef struct {
    const char* name;
    const char* auth_header_format;  // "Authorization: Bearer %s" or "x-api-key: %s"
    int requires_auth;
    const char* default_model;
    size_t default_dimension;
} EmbeddingProviderCapabilities;

// Provider interface - all embedding providers must implement these functions
typedef struct EmbeddingProvider {
    EmbeddingProviderCapabilities capabilities;
    
    // Provider detection
    int (*detect_provider)(const char* api_url);
    
    // Request building
    char* (*build_request_json)(const struct EmbeddingProvider* provider,
                               const char* model,
                               const char* text);
    
    // HTTP headers
    int (*build_headers)(const struct EmbeddingProvider* provider,
                        const char* api_key,
                        const char** headers,
                        int max_headers);
    
    // Response parsing
    int (*parse_response)(const struct EmbeddingProvider* provider,
                         const char* json_response,
                         embedding_vector_t* embedding);

} EmbeddingProvider;

// Provider registry and factory
PTRARRAY_DECLARE(EmbeddingProviderRegistry, EmbeddingProvider)

// Core provider management functions
int init_embedding_provider_registry(EmbeddingProviderRegistry* registry);
int register_embedding_provider(EmbeddingProviderRegistry* registry, EmbeddingProvider* provider);
EmbeddingProvider* detect_embedding_provider_for_url(EmbeddingProviderRegistry* registry, const char* api_url);
void cleanup_embedding_provider_registry(EmbeddingProviderRegistry* registry);

// Built-in provider registration functions
int register_openai_embedding_provider(EmbeddingProviderRegistry* registry);
int register_local_embedding_provider(EmbeddingProviderRegistry* registry);

#endif // EMBEDDING_PROVIDER_H