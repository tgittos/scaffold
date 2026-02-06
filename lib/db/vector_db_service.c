#include "vector_db_service.h"
#include "../services/services.h"
#include "llm/embeddings_service.h"
#include "util/common_utils.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct vector_db_service {
    vector_db_t* database;
    pthread_mutex_t mutex;
};

vector_db_service_t* vector_db_service_create(void) {
    vector_db_service_t* service = malloc(sizeof(vector_db_service_t));
    if (service == NULL) {
        return NULL;
    }

    service->database = vector_db_create();
    if (service->database == NULL) {
        free(service);
        return NULL;
    }

    if (pthread_mutex_init(&service->mutex, NULL) != 0) {
        vector_db_destroy(service->database);
        free(service);
        return NULL;
    }

    return service;
}

void vector_db_service_destroy(vector_db_service_t* service) {
    if (service == NULL) {
        return;
    }

    pthread_mutex_lock(&service->mutex);
    if (service->database != NULL) {
        vector_db_destroy(service->database);
        service->database = NULL;
    }
    pthread_mutex_unlock(&service->mutex);

    pthread_mutex_destroy(&service->mutex);
    free(service);
}

vector_db_t* vector_db_service_get_database(vector_db_service_t* service) {
    if (service == NULL) {
        return NULL;
    }
    return service->database;
}

vector_db_error_t vector_db_service_ensure_index(vector_db_service_t* service, const char* name, const index_config_t* config) {
    if (service == NULL || name == NULL || config == NULL) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }

    pthread_mutex_lock(&service->mutex);

    if (vector_db_has_index(service->database, name)) {
        pthread_mutex_unlock(&service->mutex);
        return VECTOR_DB_OK;
    }

    vector_db_error_t result = vector_db_create_index(service->database, name, config);

    pthread_mutex_unlock(&service->mutex);
    return result;
}

int vector_db_service_add_text(Services* services, const char* index_name,
                               const char* text, const char* type,
                               const char* source, const char* metadata_json) {
    if (services == NULL || index_name == NULL || text == NULL) return -1;

    document_store_t* store = services_get_document_store(services);
    if (store == NULL) return -1;

    embeddings_service_t* emb = services_get_embeddings(services);
    if (!embeddings_service_is_configured(emb)) {
        size_t fallback_dim = 1536;
        float *zero_embedding = calloc(fallback_dim, sizeof(float));
        if (zero_embedding == NULL) return -1;

        int result = document_store_add(store, index_name, text, zero_embedding,
                                       fallback_dim, type, source, metadata_json);
        free(zero_embedding);
        return result;
    }

    embedding_vector_t embedding;
    if (embeddings_service_get_vector(emb, text, &embedding) != 0) {
        return -1;
    }

    int result = document_store_add(store, index_name, text, embedding.data,
                                   embedding.dimension, type, source, metadata_json);

    embeddings_service_free_embedding(&embedding);
    return result;
}

document_search_results_t* vector_db_service_search_text(Services* services,
                                                         const char* index_name,
                                                         const char* query_text,
                                                         size_t k) {
    if (services == NULL || index_name == NULL || query_text == NULL) return NULL;

    document_store_t* store = services_get_document_store(services);
    if (store == NULL) return NULL;

    embeddings_service_t* emb = services_get_embeddings(services);
    if (!embeddings_service_is_configured(emb)) {
        return NULL;
    }

    embedding_vector_t embedding;
    if (embeddings_service_get_vector(emb, query_text, &embedding) != 0) {
        return NULL;
    }

    document_search_results_t* results = document_store_search(store, index_name,
                                                              embedding.data, embedding.dimension, k);

    embeddings_service_free_embedding(&embedding);
    return results;
}

char** vector_db_service_list_indices(vector_db_service_t* service, size_t* count) {
    if (service == NULL || service->database == NULL) {
        if (count) *count = 0;
        return NULL;
    }
    return vector_db_list_indices(service->database, count);
}

size_t vector_db_service_get_index_size(vector_db_service_t* service, const char* index_name) {
    if (service == NULL || service->database == NULL) return 0;
    return vector_db_get_index_size(service->database, index_name);
}

size_t vector_db_service_get_index_capacity(vector_db_service_t* service, const char* index_name) {
    if (service == NULL || service->database == NULL) return 0;
    return vector_db_get_index_capacity(service->database, index_name);
}

bool vector_db_service_has_index(vector_db_service_t* service, const char* index_name) {
    if (service == NULL || service->database == NULL) return false;
    return vector_db_has_index(service->database, index_name);
}

vector_db_error_t vector_db_service_update_vector(vector_db_service_t* service, const char* index_name,
                                                   const vector_t* vector, size_t label) {
    if (service == NULL || service->database == NULL) return VECTOR_DB_ERROR_INVALID_PARAM;
    return vector_db_update_vector(service->database, index_name, vector, label);
}
