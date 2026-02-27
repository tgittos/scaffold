/**
 * lib/services/services.h - Service container for dependency injection
 *
 * Provides a container for injecting service dependencies into agents,
 * enabling testability and flexible configuration.
 */

#ifndef LIB_SERVICES_SERVICES_H
#define LIB_SERVICES_SERVICES_H

/* Service types */
#include "../ipc/message_store.h"
#include "db/vector_db_service.h"
#include "db/task_store.h"
#include "db/document_store.h"
#include "db/metadata_store.h"
#include "db/goal_store.h"
#include "db/action_store.h"
#include "llm/embeddings_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Service container for dependency injection.
 * Holds references to services that can be injected into agents.
 * The container owns its services and destroys them on cleanup.
 */
typedef struct Services {
    /** Message store for inter-agent communication */
    message_store_t* message_store;

    /** Vector database service for semantic search */
    vector_db_service_t* vector_db;

    /** Embeddings service for text vectorization */
    embeddings_service_t* embeddings;

    /** Task store for persistent todos */
    task_store_t* task_store;

    /** Document store for vector-backed document storage */
    document_store_t* document_store;

    /** Metadata store for chunk metadata */
    metadata_store_t* metadata_store;

    /** Goal store for GOAP goal persistence */
    goal_store_t* goal_store;

    /** Action store for GOAP action persistence */
    action_store_t* action_store;
} Services;

/**
 * Create a services container with default service instances.
 *
 * @return New services container, or NULL on failure.
 *         Caller must free with services_destroy().
 */
Services* services_create_default(void);

/**
 * Create an empty services container for custom injection.
 * Caller must populate service pointers before use.
 *
 * @return Empty services container, or NULL on failure.
 *         Caller must free with services_destroy().
 */
Services* services_create_empty(void);

/**
 * Destroy a services container and all owned services.
 *
 * @param services Services to destroy (may be NULL)
 */
void services_destroy(Services* services);

message_store_t* services_get_message_store(Services* services);
vector_db_service_t* services_get_vector_db(Services* services);
embeddings_service_t* services_get_embeddings(Services* services);
task_store_t* services_get_task_store(Services* services);
document_store_t* services_get_document_store(Services* services);
metadata_store_t* services_get_metadata_store(Services* services);
goal_store_t* services_get_goal_store(Services* services);
action_store_t* services_get_action_store(Services* services);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SERVICES_SERVICES_H */
