#include "embeddings.h"
#include "embedding_provider.h"
#include "../network/http_client.h"
#include <cJSON.h>
#include "../utils/debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Provider registration functions
int register_openai_embedding_provider(EmbeddingProviderRegistry* registry);
int register_local_embedding_provider(EmbeddingProviderRegistry* registry);

// Global embedding provider registry
static EmbeddingProviderRegistry g_embedding_registry = {0};
static int g_embedding_registry_initialized = 0;

static int init_embedding_registry_once(void) {
    if (g_embedding_registry_initialized) {
        return 0;
    }
    
    if (init_embedding_provider_registry(&g_embedding_registry) != 0) {
        return -1;
    }
    
    // Register built-in providers (order matters - specific providers first, fallback last)
    register_openai_embedding_provider(&g_embedding_registry);
    register_local_embedding_provider(&g_embedding_registry);
    
    g_embedding_registry_initialized = 1;
    return 0;
}

static char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

int embeddings_init(embeddings_config_t *config, const char *model, 
                    const char *api_key, const char *api_url) {
    if (config == NULL) {
        return -1;
    }
    
    // Initialize provider registry if not done yet
    if (init_embedding_registry_once() != 0) {
        return -1;
    }
    
    // Set default URL if not provided
    const char *url = api_url ? api_url : "https://api.openai.com/v1/embeddings";
    
    // Detect provider based on URL
    EmbeddingProvider *provider = detect_embedding_provider_for_url(&g_embedding_registry, url);
    if (provider == NULL) {
        fprintf(stderr, "Error: No suitable embedding provider found for URL: %s\n", url);
        return -1;
    }
    
    // Use default model from provider if not specified
    const char *final_model = model;
    if (final_model == NULL && provider->capabilities.default_model) {
        final_model = provider->capabilities.default_model;
    }
    if (final_model == NULL) {
        final_model = "text-embedding-3-small"; // Ultimate fallback
    }
    
    config->model = safe_strdup(final_model);
    config->api_key = safe_strdup(api_key);
    config->api_url = safe_strdup(url);
    config->provider = provider;
    
    if (config->model == NULL || config->api_url == NULL) {
        embeddings_cleanup(config);
        return -1;
    }
    
    // Check if auth is required but not provided
    if (provider->capabilities.requires_auth && (api_key == NULL || strlen(api_key) == 0)) {
        fprintf(stderr, "Warning: Provider %s requires authentication but no API key provided\n", 
                provider->capabilities.name);
        // Don't fail here - some providers might work without auth in certain configurations
    }
    
    return 0;
}

// These functions are now handled by individual providers

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
    
    // Build request JSON using provider
    char *request_json = provider->build_request_json(provider, config->model, text);
    if (request_json == NULL) {
        fprintf(stderr, "Error: Failed to build embeddings request\n");
        return -1;
    }
    
    debug_printf("Embeddings request JSON: %s\n", request_json);
    
    // Set up headers using provider
    const char *headers[10];
    int header_count = provider->build_headers(provider, config->api_key, headers, 10);
    if (header_count < 0) {
        fprintf(stderr, "Error: Failed to build headers\n");
        free(request_json);
        return -1;
    }
    
    // Null-terminate the headers array
    if (header_count < 10) {
        headers[header_count] = NULL;
    }
    
    // Make API request
    struct HTTPResponse response = {0};
    int result = http_post_with_headers(config->api_url, request_json, headers, &response);
    
    free(request_json);
    
    if (result != 0 || response.data == NULL) {
        fprintf(stderr, "Error: Failed to get embeddings from API\n");
        cleanup_response(&response);
        return -1;
    }
    
    // Check for API errors
    if (strstr(response.data, "\"error\"") != NULL) {
        debug_fprintf(stderr, "API Error: %s\n", response.data);
        cleanup_response(&response);
        return -1;
    }
    
    debug_printf_json("Embeddings response: ", response.data);
    
    // Parse the response using provider
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
        config->provider = NULL; // Provider is not owned by config, just a reference
    }
}