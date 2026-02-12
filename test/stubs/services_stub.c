/**
 * test/stubs/services_stub.c - Minimal services stub for messaging tests
 *
 * Provides only the accessor functions needed by message_poller and
 * notification_formatter tests, avoiding the full dependency chain of
 * services_create_default().
 *
 * NOTE: Unlike the real services.c implementation, this stub returns NULL
 * for non-message-store services (vector_db, embeddings, task_store) when
 * the services pointer is NULL. The real implementation would return the
 * singleton instance. This keeps test dependencies minimal but means tests
 * using this stub cannot rely on singleton fallback for those services.
 */

#include "services/services.h"
#include "ipc/message_store.h"
#include <stdlib.h>

Services* services_create_default(void) {
    return NULL;
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
    services->metadata_store = NULL;
    services->goal_store = NULL;
    services->action_store = NULL;
    services->use_singletons = false;

    return services;
}

void services_destroy(Services* services) {
    free(services);
}

message_store_t* services_get_message_store(Services* services) {
    return (services != NULL) ? services->message_store : NULL;
}

vector_db_service_t* services_get_vector_db(Services* services) {
    if (services != NULL && services->vector_db != NULL) {
        return services->vector_db;
    }
    return NULL;
}

embeddings_service_t* services_get_embeddings(Services* services) {
    if (services != NULL && services->embeddings != NULL) {
        return services->embeddings;
    }
    return NULL;
}

task_store_t* services_get_task_store(Services* services) {
    if (services != NULL && services->task_store != NULL) {
        return services->task_store;
    }
    return NULL;
}

document_store_t* services_get_document_store(Services* services) {
    if (services != NULL && services->document_store != NULL) {
        return services->document_store;
    }
    return NULL;
}

metadata_store_t* services_get_metadata_store(Services* services) {
    if (services != NULL && services->metadata_store != NULL) {
        return services->metadata_store;
    }
    return NULL;
}

goal_store_t* services_get_goal_store(Services* services) {
    if (services != NULL && services->goal_store != NULL) {
        return services->goal_store;
    }
    return NULL;
}

action_store_t* services_get_action_store(Services* services) {
    if (services != NULL && services->action_store != NULL) {
        return services->action_store;
    }
    return NULL;
}
