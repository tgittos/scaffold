#define LOG_MODULE     LOG_MOD_LLM
#define LOG_MODULE_STR "llm"
#include "../util/log.h"
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
    size_t dimension;
    pthread_mutex_t mutex;
};

/*
 * Probe the embedding endpoint with a short text and cache the dimension
 * of the vector the model actually returns. The index-creation code must
 * use this, because models of the same provider differ in dimension.
 */
static int probe_embedding_dimension(embeddings_service_t* service) {
    embedding_vector_t embedding = {0};
    if (embeddings_get_vector(&service->config, "scaffold dimension probe", &embedding) != 0) {
        return -1;
    }

    if (embedding.dimension == 0) {
        embeddings_free_vector(&embedding);
        return -1;
    }

    service->dimension = embedding.dimension;
    embeddings_free_vector(&embedding);
    return 0;
}

/*
 * Initialize config and probe the model. On probe failure the embedding
 * feature is disabled entirely: consumers fall back to the unconfigured
 * behavior (zero embeddings, no semantic search).
 */
static int configure_and_probe(embeddings_service_t* service) {
    const char *api_key = getenv("OPENAI_API_KEY");
    const char *model = getenv("EMBEDDING_MODEL");
    const char *api_url = getenv("EMBEDDING_API_URL");
    if (!api_url) api_url = getenv("OPENAI_API_URL");

    if (!model) model = "text-embedding-3-small";

    if (!api_key || embeddings_init(&service->config, model, api_key, api_url) != 0) {
        return -1;
    }

    if (probe_embedding_dimension(service) != 0) {
        LOG_ERROR("Embeddings disabled: probe request to %s failed", api_url ? api_url : "(default)");
        embeddings_cleanup(&service->config);
        return -1;
    }

    LOG_INFO("Embeddings ready: model=%s dimension=%zu",
             service->config.model ? service->config.model : "(default)",
             service->dimension);
    return 0;
}

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

    if (configure_and_probe(service) == 0) {
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
    if (service == NULL || !service->configured || service->dimension == 0) {
        return EMBEDDINGS_DIM_DEFAULT;
    }

    return service->dimension;
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
    }
    service->configured = 0;
    service->dimension = 0;

    /* A failed probe is a valid outcome: it disables the feature.
     * The caller checks is_configured()/get_dimension() for the new state. */
    if (configure_and_probe(service) == 0) {
        service->configured = 1;
    }

    pthread_mutex_unlock(&service->mutex);

    return 0;
}
