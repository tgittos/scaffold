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

    services->message_store = message_store_create(NULL);
    services->vector_db = vector_db_service_create();
    services->embeddings = embeddings_service_create();
    services->task_store = task_store_create(NULL);

    document_store_set_services(services);
    services->document_store = document_store_create(NULL);

    services->use_singletons = false;

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
    services->document_store = NULL;
    services->use_singletons = false;

    return services;
}

void services_destroy(Services* services) {
    if (services == NULL) {
        return;
    }

    if (services->message_store != NULL) {
        message_store_destroy(services->message_store);
    }
    if (services->vector_db != NULL) {
        vector_db_service_destroy(services->vector_db);
    }
    if (services->embeddings != NULL) {
        embeddings_service_destroy(services->embeddings);
    }
    if (services->task_store != NULL) {
        task_store_destroy(services->task_store);
    }
    if (services->document_store != NULL) {
        document_store_destroy(services->document_store);
    }

    free(services);
}

message_store_t* services_get_message_store(Services* services) {
    return (services != NULL) ? services->message_store : NULL;
}

vector_db_service_t* services_get_vector_db(Services* services) {
    return (services != NULL) ? services->vector_db : NULL;
}

embeddings_service_t* services_get_embeddings(Services* services) {
    return (services != NULL) ? services->embeddings : NULL;
}

task_store_t* services_get_task_store(Services* services) {
    return (services != NULL) ? services->task_store : NULL;
}

document_store_t* services_get_document_store(Services* services) {
    return (services != NULL) ? services->document_store : NULL;
}
