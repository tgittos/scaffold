#include "vector_db_service.h"
#include "../utils/common_utils.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct vector_db_service {
    vector_db_t* database;
    pthread_mutex_t mutex;
};

static vector_db_service_t* g_service_instance = NULL;
static pthread_once_t g_service_once = PTHREAD_ONCE_INIT;

static void create_service_instance(void) {
    g_service_instance = malloc(sizeof(vector_db_service_t));
    if (g_service_instance == NULL) {
        return;
    }
    
    g_service_instance->database = vector_db_create();
    if (g_service_instance->database == NULL) {
        free(g_service_instance);
        g_service_instance = NULL;
        return;
    }
    
    if (pthread_mutex_init(&g_service_instance->mutex, NULL) != 0) {
        vector_db_destroy(g_service_instance->database);
        free(g_service_instance);
        g_service_instance = NULL;
        return;
    }
}

vector_db_service_t* vector_db_service_get_instance(void) {
    pthread_once(&g_service_once, create_service_instance);
    return g_service_instance;
}

vector_db_t* vector_db_service_get_database(void) {
    vector_db_service_t* service = vector_db_service_get_instance();
    if (service == NULL) {
        return NULL;
    }
    return service->database;
}

vector_db_error_t vector_db_service_ensure_index(const char* name, const index_config_t* config) {
    if (name == NULL || config == NULL) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    vector_db_service_t* service = vector_db_service_get_instance();
    if (service == NULL) {
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    pthread_mutex_lock(&service->mutex);
    
    // Check if index already exists
    if (vector_db_has_index(service->database, name)) {
        pthread_mutex_unlock(&service->mutex);
        return VECTOR_DB_OK;
    }
    
    // Create the index
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

index_config_t vector_db_service_get_document_config(size_t dimension) {
    index_config_t config = {
        .dimension = dimension,
        .max_elements = 50000,
        .M = 32,
        .ef_construction = 400,
        .random_seed = 123,
        .metric = safe_strdup("cosine")
    };
    return config;
}

void vector_db_service_cleanup(void) {
    if (g_service_instance != NULL) {
        pthread_mutex_lock(&g_service_instance->mutex);
        if (g_service_instance->database != NULL) {
            vector_db_destroy(g_service_instance->database);
            g_service_instance->database = NULL;
        }
        pthread_mutex_unlock(&g_service_instance->mutex);
        
        pthread_mutex_destroy(&g_service_instance->mutex);
        free(g_service_instance);
        g_service_instance = NULL;
    }
}