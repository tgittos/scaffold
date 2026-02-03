#ifndef EMBEDDING_PROVIDER_H
#define EMBEDDING_PROVIDER_H

#include "embeddings.h"
#include "util/ptrarray.h"

struct EmbeddingProvider;

typedef struct {
    const char* name;
    const char* auth_header_format;  /* e.g. "Authorization: Bearer %s" */
    int requires_auth;
    const char* default_model;
    size_t default_dimension;
} EmbeddingProviderCapabilities;

typedef struct EmbeddingProvider {
    EmbeddingProviderCapabilities capabilities;
    
    int (*detect_provider)(const char* api_url);

    char* (*build_request_json)(const struct EmbeddingProvider* provider,
                               const char* model,
                               const char* text);
    
    int (*build_headers)(const struct EmbeddingProvider* provider,
                        const char* api_key,
                        const char** headers,
                        int max_headers);
    
    int (*parse_response)(const struct EmbeddingProvider* provider,
                         const char* json_response,
                         embedding_vector_t* embedding);

} EmbeddingProvider;

PTRARRAY_DECLARE(EmbeddingProviderRegistry, EmbeddingProvider)

int init_embedding_provider_registry(EmbeddingProviderRegistry* registry);
int register_embedding_provider(EmbeddingProviderRegistry* registry, EmbeddingProvider* provider);
EmbeddingProvider* detect_embedding_provider_for_url(EmbeddingProviderRegistry* registry, const char* api_url);
void cleanup_embedding_provider_registry(EmbeddingProviderRegistry* registry);

int register_openai_embedding_provider(EmbeddingProviderRegistry* registry);
int register_local_embedding_provider(EmbeddingProviderRegistry* registry);

#endif // EMBEDDING_PROVIDER_H