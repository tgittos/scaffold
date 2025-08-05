#ifndef VECTOR_DB_SERVICE_H
#define VECTOR_DB_SERVICE_H

#include "vector_db.h"

/**
 * Vector Database Service - Centralized singleton management
 * 
 * This service provides a centralized way to access the vector database
 * across all modules, ensuring consistent state and preventing multiple
 * singleton instances.
 */

typedef struct vector_db_service vector_db_service_t;

/**
 * Get the singleton vector database service instance
 * 
 * @return Vector database service instance (never NULL)
 */
vector_db_service_t* vector_db_service_get_instance(void);

/**
 * Get the shared vector database instance
 * 
 * @return Vector database instance (never NULL)
 */
vector_db_t* vector_db_service_get_database(void);

/**
 * Ensure an index exists with the given configuration
 * Creates the index if it doesn't exist, validates if it does
 * 
 * @param name Index name
 * @param config Index configuration
 * @return Vector database error code
 */
vector_db_error_t vector_db_service_ensure_index(const char* name, const index_config_t* config);

/**
 * Get a standard configuration for memory indices
 * 
 * @param dimension Vector dimension
 * @return Standard memory index configuration
 */
index_config_t vector_db_service_get_memory_config(size_t dimension);

/**
 * Get a standard configuration for document indices
 * 
 * @param dimension Vector dimension
 * @return Standard document index configuration
 */
index_config_t vector_db_service_get_document_config(size_t dimension);

/**
 * Cleanup vector database service
 * Should be called on application shutdown
 */
void vector_db_service_cleanup(void);

#endif // VECTOR_DB_SERVICE_H