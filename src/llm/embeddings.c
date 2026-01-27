#include "embeddings.h"
#include "embedding_provider.h"
#include "../network/http_client.h"
#include "../utils/common_utils.h"
#include <cJSON.h>
#include "../utils/debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

int register_openai_embedding_provider(EmbeddingProviderRegistry* registry);
int register_local_embedding_provider(EmbeddingProviderRegistry* registry);

static EmbeddingProviderRegistry g_embedding_registry = {0};
static pthread_once_t g_registry_init_once = PTHREAD_ONCE_INIT;
static int g_registry_init_result = -1;

static void init_embedding_registry_internal(void) {
    if (init_embedding_provider_registry(&g_embedding_registry) != 0) {
        g_registry_init_result = -1;
        return;
    }

    // Order matters: specific providers first, generic fallback last
    if (register_openai_embedding_provider(&g_embedding_registry) != 0) {
        cleanup_embedding_provider_registry(&g_embedding_registry);
        g_registry_init_result = -1;
        return;
    }

    if (register_local_embedding_provider(&g_embedding_registry) != 0) {
        cleanup_embedding_provider_registry(&g_embedding_registry);
        g_registry_init_result = -1;
        return;
    }

    g_registry_init_result = 0;
}

/* pthread_once has no retry: if init fails, all future calls also fail. */
static int init_embedding_registry_once(void) {
    pthread_once(&g_registry_init_once, init_embedding_registry_internal);
    return g_registry_init_result;
}

int embeddings_init(embeddings_config_t *config, const char *model,
                    const char *api_key, const char *api_url) {
    if (config == NULL) {
        return -1;
    }
    
    if (init_embedding_registry_once() != 0) {
        return -1;
    }
    
    const char *url = api_url ? api_url : "https://api.openai.com/v1/embeddings";

    EmbeddingProvider *provider = detect_embedding_provider_for_url(&g_embedding_registry, url);
    if (provider == NULL) {
        fprintf(stderr, "Error: No suitable embedding provider found for URL: %s\n", url);
        return -1;
    }
    
    const char *final_model = model;
    if (final_model == NULL && provider->capabilities.default_model) {
        final_model = provider->capabilities.default_model;
    }
    if (final_model == NULL) {
        final_model = "text-embedding-3-small";
    }
    
    config->model = safe_strdup(final_model);
    config->api_key = safe_strdup(api_key);
    config->api_url = safe_strdup(url);
    config->provider = provider;
    
    if (config->model == NULL || config->api_url == NULL) {
        embeddings_cleanup(config);
        return -1;
    }
    
    // Warn but don't fail: some providers work without auth in certain configurations
    if (provider->capabilities.requires_auth && (api_key == NULL || strlen(api_key) == 0)) {
        fprintf(stderr, "Warning: Provider %s requires authentication but no API key provided\n",
                provider->capabilities.name);
    }
    
    return 0;
}

int embeddings_get_vector(const embeddings_config_t *config, const char *text,
                          embedding_vector_t *embedding) {
    if (config == NULL || text == NULL || embedding == NULL) {
        return -1;
    }
    
    if (config->provider == NULL) {
        fprintf(stderr, "Error: No embedding provider configured\n");
        return -1;
    }
    
    EmbeddingProvider *provider = config->provider;

    if (provider->build_request_json == NULL) {
        fprintf(stderr, "Error: Provider missing build_request_json implementation\n");
        return -1;
    }
    if (provider->build_headers == NULL) {
        fprintf(stderr, "Error: Provider missing build_headers implementation\n");
        return -1;
    }
    if (provider->parse_response == NULL) {
        fprintf(stderr, "Error: Provider missing parse_response implementation\n");
        return -1;
    }

    char *request_json = provider->build_request_json(provider, config->model, text);
    if (request_json == NULL) {
        fprintf(stderr, "Error: Failed to build embeddings request\n");
        return -1;
    }

    debug_printf("Embeddings request JSON: %s\n", request_json);

    const char *headers[11]; /* +1 for NULL terminator */
    const int max_headers = 10;
    int header_count = provider->build_headers(provider, config->api_key, headers, max_headers);
    if (header_count < 0 || header_count > max_headers) {
        fprintf(stderr, "Error: Invalid header count from provider\n");
        free(request_json);
        return -1;
    }

    headers[header_count] = NULL;

    struct HTTPResponse response = {0};
    int result = http_post_with_headers(config->api_url, request_json, headers, &response);

    free(request_json);

    if (result != 0 || response.data == NULL) {
        fprintf(stderr, "Error: Failed to get embeddings from API\n");
        cleanup_response(&response);
        return -1;
    }

    if (strstr(response.data, "\"error\"") != NULL) {
        debug_fprintf(stderr, "API Error: %s\n", response.data);
        cleanup_response(&response);
        return -1;
    }

    debug_printf_json("Embeddings response: ", response.data);

    result = provider->parse_response(provider, response.data, embedding);
    cleanup_response(&response);

    return result;
}

void embeddings_free_vector(embedding_vector_t *embedding) {
    if (embedding != NULL && embedding->data != NULL) {
        free(embedding->data);
        embedding->data = NULL;
        embedding->dimension = 0;
    }
}

void embeddings_cleanup(embeddings_config_t *config) {
    if (config != NULL) {
        free(config->model);
        free(config->api_key);
        free(config->api_url);
        config->model = NULL;
        config->api_key = NULL;
        config->api_url = NULL;
        config->provider = NULL; /* not owned, just a reference to the global registry */
    }
}