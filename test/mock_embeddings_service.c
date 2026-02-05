/**
 * Mock embeddings service — link-time replacement for lib/llm/embeddings_service.c
 *
 * Calls mock_embeddings_get_vector() directly. No HTTP, no env vars, no config.
 */

#include "llm/embeddings_service.h"
#include "mock_embeddings.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct embeddings_service {
    int configured;
    pthread_mutex_t mutex;
};

embeddings_service_t* embeddings_service_create(void) {
    embeddings_service_t* service = malloc(sizeof(embeddings_service_t));
    if (service == NULL) return NULL;

    memset(service, 0, sizeof(embeddings_service_t));
    if (pthread_mutex_init(&service->mutex, NULL) != 0) {
        free(service);
        return NULL;
    }

    service->configured = 1;
    return service;
}

void embeddings_service_destroy(embeddings_service_t* service) {
    if (service == NULL) return;
    pthread_mutex_destroy(&service->mutex);
    free(service);
}

int embeddings_service_is_configured(embeddings_service_t* service) {
    return service != NULL && service->configured;
}

int embeddings_service_get_vector(embeddings_service_t* service, const char *text, embedding_vector_t *embedding) {
    if (service == NULL || text == NULL || embedding == NULL) return -1;
    if (!service->configured) return -1;

    float* data = malloc(MOCK_EMBEDDING_DIM * sizeof(float));
    if (data == NULL) return -1;

    pthread_mutex_lock(&service->mutex);
    int result = mock_embeddings_get_vector(text, data);
    pthread_mutex_unlock(&service->mutex);

    if (result != 0) {
        free(data);
        return -1;
    }

    embedding->data = data;
    embedding->dimension = MOCK_EMBEDDING_DIM;
    return 0;
}

vector_t* embeddings_service_text_to_vector(embeddings_service_t* service, const char *text) {
    if (service == NULL || text == NULL) return NULL;

    embedding_vector_t embedding = {0};
    if (embeddings_service_get_vector(service, text, &embedding) != 0) return NULL;

    vector_t* vector = malloc(sizeof(vector_t));
    if (vector == NULL) {
        free(embedding.data);
        return NULL;
    }

    vector->data = embedding.data;
    vector->dimension = embedding.dimension;
    return vector;
}

size_t embeddings_service_get_dimension(embeddings_service_t* service) {
    (void)service;
    return MOCK_EMBEDDING_DIM;
}

void embeddings_service_free_embedding(embedding_vector_t *embedding) {
    if (embedding != NULL) {
        free(embedding->data);
        embedding->data = NULL;
        embedding->dimension = 0;
    }
}

void embeddings_service_free_vector(vector_t* vector) {
    if (vector != NULL) {
        free(vector->data);
        free(vector);
    }
}

int embeddings_service_reinitialize(embeddings_service_t* service) {
    (void)service;
    return 0;
}
