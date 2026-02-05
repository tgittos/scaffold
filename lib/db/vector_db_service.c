#include "vector_db_service.h"
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

index_config_t vector_db_service_get_memory_config(size_t dimension) {
    index_config_t config = {
        .dimension = dimension,
        .max_elements = 100000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 42,
        .metric = safe_strdup("cosine")
    };
    return config;
}
