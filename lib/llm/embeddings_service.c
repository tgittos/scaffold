#include "embeddings_service.h"
#include "embedding_provider.h"
#include "db/vector_db.h"
#include "util/common_utils.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct embeddings_service {
    embeddings_config_t config;
    int configured;
    pthread_mutex_t mutex;
};

embeddings_service_t* embeddings_service_create(void) {
    embeddings_service_t* service = malloc(sizeof(embeddings_service_t));
    if (service == NULL) {
        return NULL;
    }

    memset(service, 0, sizeof(embeddings_service_t));

    if (pthread_mutex_init(&service->mutex, NULL) != 0) {
        free(service);
        return NULL;
    }

    const char *api_key = getenv("OPENAI_API_KEY");
    const char *model = getenv("EMBEDDING_MODEL");
    const char *api_url = getenv("EMBEDDING_API_URL");
    if (!api_url) api_url = getenv("OPENAI_API_URL");

    if (!model) model = "text-embedding-3-small";

    if (api_key && embeddings_init(&service->config, model, api_key, api_url) == 0) {
        service->configured = 1;
    }

    return service;
}

void embeddings_service_destroy(embeddings_service_t* service) {
    if (service == NULL) {
        return;
    }

    pthread_mutex_lock(&service->mutex);
    if (service->configured) {
        embeddings_cleanup(&service->config);
        service->configured = 0;
    }
    pthread_mutex_unlock(&service->mutex);

    pthread_mutex_destroy(&service->mutex);
    free(service);
}

int embeddings_service_is_configured(embeddings_service_t* service) {
    return service != NULL && service->configured;
}

int embeddings_service_get_vector(embeddings_service_t* service, const char *text, embedding_vector_t *embedding) {
    if (service == NULL || text == NULL || embedding == NULL) {
        return -1;
    }

    if (!service->configured) {
        return -1;
    }

    pthread_mutex_lock(&service->mutex);
    int result = embeddings_get_vector(&service->config, text, embedding);
    pthread_mutex_unlock(&service->mutex);

    return result;
}

vector_t* embeddings_service_text_to_vector(embeddings_service_t* service, const char *text) {
    if (service == NULL || text == NULL) {
        return NULL;
    }

    embedding_vector_t embedding = {0};
    if (embeddings_service_get_vector(service, text, &embedding) != 0) {
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

size_t embeddings_service_get_dimension(embeddings_service_t* service) {
    if (service == NULL || !service->configured) {
        return 0;
    }

    if (service->config.provider != NULL &&
        service->config.provider->capabilities.default_dimension > 0) {
        return service->config.provider->capabilities.default_dimension;
    }

    return 1536;
}

void embeddings_service_free_embedding(embedding_vector_t *embedding) {
    embeddings_free_vector(embedding);
}

void embeddings_service_free_vector(vector_t* vector) {
    if (vector != NULL) {
        free(vector->data);
        free(vector);
    }
}

int embeddings_service_reinitialize(embeddings_service_t* service) {
    if (service == NULL) {
        return -1;
    }

    pthread_mutex_lock(&service->mutex);

    if (service->configured) {
        embeddings_cleanup(&service->config);
        service->configured = 0;
    }

    const char *api_key = getenv("OPENAI_API_KEY");
    const char *model = getenv("EMBEDDING_MODEL");
    const char *api_url = getenv("EMBEDDING_API_URL");
    if (!api_url) api_url = getenv("OPENAI_API_URL");

    if (!model) model = "text-embedding-3-small";

    if (api_key && embeddings_init(&service->config, model, api_key, api_url) == 0) {
        service->configured = 1;
    }

    int result = service->configured ? 0 : -1;
    pthread_mutex_unlock(&service->mutex);

    return result;
}
