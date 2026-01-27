#include "embedding_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PTRARRAY_DEFINE(EmbeddingProviderRegistry, EmbeddingProvider)

int init_embedding_provider_registry(EmbeddingProviderRegistry* registry) {
    // Providers are static, so we don't own them - pass NULL destructor
    return EmbeddingProviderRegistry_init(registry, NULL);
}

int register_embedding_provider(EmbeddingProviderRegistry* registry, EmbeddingProvider* provider) {
    return EmbeddingProviderRegistry_push(registry, provider);
}

EmbeddingProvider* detect_embedding_provider_for_url(EmbeddingProviderRegistry* registry, const char* api_url) {
    if (registry == NULL || api_url == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        EmbeddingProvider* provider = registry->data[i];
        if (provider->detect_provider && provider->detect_provider(api_url)) {
            return provider;
        }
    }

    return NULL;
}

void cleanup_embedding_provider_registry(EmbeddingProviderRegistry* registry) {
    EmbeddingProviderRegistry_destroy(registry);
}
