#include "llm_provider.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PTRARRAY_DEFINE(ProviderRegistry, LLMProvider)

int init_provider_registry(ProviderRegistry* registry) {
    // Providers are static, so we don't own them - pass NULL destructor
    return ProviderRegistry_init(registry, NULL);
}

int register_provider(ProviderRegistry* registry, LLMProvider* provider) {
    return ProviderRegistry_push(registry, provider);
}

LLMProvider* detect_provider_for_url(ProviderRegistry* registry, const char* api_url) {
    if (registry == NULL || api_url == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        LLMProvider* provider = registry->data[i];
        if (provider->detect_provider && provider->detect_provider(api_url)) {
            return provider;
        }
    }

    return NULL; // No matching provider found
}

void cleanup_provider_registry(ProviderRegistry* registry) {
    ProviderRegistry_destroy(registry);
}
