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

    return NULL;
}

void cleanup_provider_registry(ProviderRegistry* registry) {
    ProviderRegistry_destroy(registry);
}

static ProviderRegistry* g_provider_registry = NULL;

ProviderRegistry* get_provider_registry(void) {
    if (g_provider_registry == NULL) {
        g_provider_registry = malloc(sizeof(ProviderRegistry));
        if (g_provider_registry != NULL) {
            if (init_provider_registry(g_provider_registry) == 0) {
                register_openai_provider(g_provider_registry);
                register_anthropic_provider(g_provider_registry);
                register_local_ai_provider(g_provider_registry);
            }
        }
    }
    return g_provider_registry;
}

void provider_registry_cleanup(void) {
    if (g_provider_registry != NULL) {
        cleanup_provider_registry(g_provider_registry);
        free(g_provider_registry);
        g_provider_registry = NULL;
    }
}
