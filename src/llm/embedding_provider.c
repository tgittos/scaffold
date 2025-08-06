#include "embedding_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int init_embedding_provider_registry(EmbeddingProviderRegistry* registry) {
    if (registry == NULL) {
        return -1;
    }
    
    registry->providers = NULL;
    registry->count = 0;
    registry->capacity = 0;
    
    return 0;
}

int register_embedding_provider(EmbeddingProviderRegistry* registry, EmbeddingProvider* provider) {
    if (registry == NULL || provider == NULL) {
        return -1;
    }
    
    // Expand capacity if needed
    if (registry->count >= registry->capacity) {
        int new_capacity = registry->capacity == 0 ? 4 : registry->capacity * 2;
        EmbeddingProvider** new_providers = realloc(registry->providers, new_capacity * sizeof(EmbeddingProvider*));
        if (new_providers == NULL) {
            return -1;
        }
        registry->providers = new_providers;
        registry->capacity = new_capacity;
    }
    
    registry->providers[registry->count] = provider;
    registry->count++;
    
    return 0;
}

EmbeddingProvider* detect_embedding_provider_for_url(EmbeddingProviderRegistry* registry, const char* api_url) {
    if (registry == NULL || api_url == NULL) {
        return NULL;
    }
    
    for (int i = 0; i < registry->count; i++) {
        EmbeddingProvider* provider = registry->providers[i];
        if (provider->detect_provider && provider->detect_provider(api_url)) {
            return provider;
        }
    }
    
    return NULL; // No matching provider found
}

void cleanup_embedding_provider_registry(EmbeddingProviderRegistry* registry) {
    if (registry == NULL) {
        return;
    }
    
    free(registry->providers);
    registry->providers = NULL;
    registry->count = 0;
    registry->capacity = 0;
}