#include "embeddings_service.h"
#include "embedding_provider.h"
#include "../utils/common_utils.h"
#include "../utils/config.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct embeddings_service {
    embeddings_config_t config;
    int configured;
    pthread_mutex_t mutex;
};

static embeddings_service_t* g_embeddings_instance = NULL;
static pthread_once_t g_embeddings_once = PTHREAD_ONCE_INIT;

static void create_embeddings_instance(void) {
    g_embeddings_instance = malloc(sizeof(embeddings_service_t));
    if (g_embeddings_instance == NULL) {
        return;
    }
    
    memset(g_embeddings_instance, 0, sizeof(embeddings_service_t));
    
    if (pthread_mutex_init(&g_embeddings_instance->mutex, NULL) != 0) {
        free(g_embeddings_instance);
        g_embeddings_instance = NULL;
        return;
    }
    
    // Initialize with configuration system
    ralph_config_t *config = config_get();
    if (config) {
        const char *api_key = config->openai_api_key;
        const char *model = config->embedding_model;
        // Prefer embedding_api_url, fall back to openai_api_url
        const char *api_url = config->embedding_api_url ? config->embedding_api_url : config->openai_api_url;
        
        if (!model) model = "text-embedding-3-small";
        
        if (api_key && embeddings_init(&g_embeddings_instance->config, model, api_key, api_url) == 0) {
            g_embeddings_instance->configured = 1;
        }
    } else {
        // Fallback to environment variables if config system not initialized
        const char *api_key = getenv("OPENAI_API_KEY");
        const char *model = getenv("EMBEDDING_MODEL");
        const char *api_url = getenv("OPENAI_API_URL");
        
        if (!model) model = "text-embedding-3-small";
        
        if (api_key && embeddings_init(&g_embeddings_instance->config, model, api_key, api_url) == 0) {
            g_embeddings_instance->configured = 1;
        }
    }
}

embeddings_service_t* embeddings_service_get_instance(void) {
    pthread_once(&g_embeddings_once, create_embeddings_instance);
    return g_embeddings_instance;
}

int embeddings_service_is_configured(void) {
    embeddings_service_t* service = embeddings_service_get_instance();
    return service != NULL && service->configured;
}

int embeddings_service_get_vector(const char *text, embedding_vector_t *embedding) {
    if (text == NULL || embedding == NULL) {
        return -1;
    }

    embeddings_service_t* service = embeddings_service_get_instance();
    if (service == NULL || !service->configured) {
        return -1;
    }

    pthread_mutex_lock(&service->mutex);
    int result = embeddings_get_vector(&service->config, text, embedding);
    pthread_mutex_unlock(&service->mutex);

    return result;
}

vector_t* embeddings_service_text_to_vector(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    
    embedding_vector_t embedding = {0};
    if (embeddings_service_get_vector(text, &embedding) != 0) {
        return NULL;
    }
    
    vector_t* vector = malloc(sizeof(vector_t));
    if (vector == NULL) {
        embeddings_free_vector(&embedding);
        return NULL;
    }
    
    vector->data = embedding.data;
    vector->dimension = embedding.dimension;
    
    return vector;
}

size_t embeddings_service_get_dimension(void) {
    embeddings_service_t* service = embeddings_service_get_instance();
    if (service == NULL || !service->configured) {
        return 0;
    }

    if (service->config.provider != NULL &&
        service->config.provider->capabilities.default_dimension > 0) {
        return service->config.provider->capabilities.default_dimension;
    }

    return 1536;
}

void embeddings_service_free_vector(vector_t* vector) {
    if (vector != NULL) {
        free(vector->data);
        free(vector);
    }
}

int embeddings_service_reinitialize(void) {
    embeddings_service_t* service = embeddings_service_get_instance();
    if (service == NULL) {
        return -1;
    }
    
    pthread_mutex_lock(&service->mutex);
    
    // Clean up existing configuration
    if (service->configured) {
        embeddings_cleanup(&service->config);
        service->configured = 0;
    }
    
    // Reinitialize with current configuration
    ralph_config_t *config = config_get();
    const char *api_key, *model, *api_url;
    
    if (config) {
        api_key = config->openai_api_key;
        model = config->embedding_model;
        // Prefer embedding_api_url, fall back to openai_api_url
        api_url = config->embedding_api_url ? config->embedding_api_url : config->openai_api_url;
    } else {
        // Fallback to environment variables
        api_key = getenv("OPENAI_API_KEY");
        model = getenv("EMBEDDING_MODEL");
        api_url = getenv("OPENAI_API_URL");
    }
    
    if (!model) model = "text-embedding-3-small";
    
    if (api_key && embeddings_init(&service->config, model, api_key, api_url) == 0) {
        service->configured = 1;
    }
    
    int result = service->configured ? 0 : -1;
    pthread_mutex_unlock(&service->mutex);
    
    return result;
}

void embeddings_service_cleanup(void) {
    if (g_embeddings_instance != NULL) {
        pthread_mutex_lock(&g_embeddings_instance->mutex);
        if (g_embeddings_instance->configured) {
            embeddings_cleanup(&g_embeddings_instance->config);
            g_embeddings_instance->configured = 0;
        }
        pthread_mutex_unlock(&g_embeddings_instance->mutex);

        pthread_mutex_destroy(&g_embeddings_instance->mutex);
        free(g_embeddings_instance);
        g_embeddings_instance = NULL;
    }
}