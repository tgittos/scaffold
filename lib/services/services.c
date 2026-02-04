/**
 * lib/services/services.c - Service container implementation
 */

#include "services.h"
#include <stdlib.h>

Services* services_create_default(void) {
    Services* services = malloc(sizeof(Services));
    if (services == NULL) {
        return NULL;
    }

    /* Get singleton instances */
    services->message_store = message_store_get_instance();
    services->vector_db = vector_db_service_get_instance();
    services->embeddings = embeddings_service_get_instance();
    services->task_store = task_store_get_instance();
    services->use_singletons = true;

    return services;
}

Services* services_create_empty(void) {
    Services* services = malloc(sizeof(Services));
    if (services == NULL) {
        return NULL;
    }

    services->message_store = NULL;
    services->vector_db = NULL;
    services->embeddings = NULL;
    services->task_store = NULL;
    services->use_singletons = false;

    return services;
}

void services_destroy(Services* services) {
    if (services == NULL) {
        return;
    }

    /* Only free the container, not the services themselves.
     * Services are either singletons (shared, managed globally)
     * or custom (caller manages their lifecycle). */
    free(services);
}

message_store_t* services_get_message_store(Services* services) {
    if (services != NULL && services->message_store != NULL) {
        return services->message_store;
    }
    return message_store_get_instance();
}

vector_db_service_t* services_get_vector_db(Services* services) {
    if (services != NULL && services->vector_db != NULL) {
        return services->vector_db;
    }
    return vector_db_service_get_instance();
}

embeddings_service_t* services_get_embeddings(Services* services) {
    if (services != NULL && services->embeddings != NULL) {
        return services->embeddings;
    }
    return embeddings_service_get_instance();
}

task_store_t* services_get_task_store(Services* services) {
    if (services != NULL && services->task_store != NULL) {
        return services->task_store;
    }
    return task_store_get_instance();
}
