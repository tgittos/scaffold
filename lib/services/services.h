/**
 * lib/services/services.h - Service container for dependency injection
 *
 * Provides a container for injecting service dependencies into agents,
 * enabling testability and flexible configuration. Services can be
 * either the default singletons or custom implementations.
 */

#ifndef LIB_SERVICES_SERVICES_H
#define LIB_SERVICES_SERVICES_H

#include <stdbool.h>

/* Re-export singleton types from src/ */
#include "../ipc/message_store.h"
#include "db/vector_db_service.h"
#include "db/task_store.h"
#include "llm/embeddings_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * SERVICE CONTAINER
 * ============================================================================= */

/**
 * Service container for dependency injection.
 * Holds references to services that can be injected into agents.
 *
 * When using singletons, services are NOT owned by this container
 * and should not be destroyed when the container is destroyed.
 * When using custom services, ownership depends on the use_singletons flag.
 */
typedef struct RalphServices {
    /** Message store for inter-agent communication */
    message_store_t* message_store;

    /** Vector database service for semantic search */
    vector_db_service_t* vector_db;

    /** Embeddings service for text vectorization */
    embeddings_service_t* embeddings;

    /** Task store for persistent todos */
    task_store_t* task_store;

    /** Flag indicating if services are singletons (don't destroy on cleanup) */
    bool use_singletons;
} RalphServices;

/* =============================================================================
 * FACTORY FUNCTIONS
 * ============================================================================= */

/**
 * Create a services container using default singleton instances.
 * The singletons are initialized on first use and shared across
 * all agents using this container.
 *
 * @return New services container with singleton references, or NULL on failure.
 *         Caller must free with ralph_services_destroy().
 */
RalphServices* ralph_services_create_default(void);

/**
 * Create an empty services container for custom injection.
 * Caller must populate service pointers before use.
 *
 * @return Empty services container, or NULL on failure.
 *         Caller must free with ralph_services_destroy().
 */
RalphServices* ralph_services_create_empty(void);

/**
 * Destroy a services container.
 *
 * NOTE: If use_singletons is true, the individual services are NOT destroyed
 * since they are shared singletons. Only the container itself is freed.
 *
 * @param services Services to destroy (may be NULL)
 */
void ralph_services_destroy(RalphServices* services);

/* =============================================================================
 * CONVENIENCE ACCESSORS
 * ============================================================================= */

/**
 * Get the message store from a services container, or the singleton if NULL.
 *
 * @param services Services container (may be NULL)
 * @return Message store instance
 */
message_store_t* ralph_services_get_message_store(RalphServices* services);

/**
 * Get the vector DB service from a services container, or the singleton if NULL.
 *
 * @param services Services container (may be NULL)
 * @return Vector DB service instance
 */
vector_db_service_t* ralph_services_get_vector_db(RalphServices* services);

/**
 * Get the embeddings service from a services container, or the singleton if NULL.
 *
 * @param services Services container (may be NULL)
 * @return Embeddings service instance
 */
embeddings_service_t* ralph_services_get_embeddings(RalphServices* services);

/**
 * Get the task store from a services container, or the singleton if NULL.
 *
 * @param services Services container (may be NULL)
 * @return Task store instance
 */
task_store_t* ralph_services_get_task_store(RalphServices* services);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SERVICES_SERVICES_H */
