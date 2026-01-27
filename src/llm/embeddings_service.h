#ifndef EMBEDDINGS_SERVICE_H
#define EMBEDDINGS_SERVICE_H

#include "embeddings.h"
#include "../db/vector_db.h"

/**
 * Embeddings Service - Centralized embedding generation and management
 * 
 * This service provides a centralized way to generate embeddings across
 * all modules, ensuring consistent configuration and efficient resource usage.
 */

typedef struct embeddings_service embeddings_service_t;

/**
 * Get the singleton embeddings service instance
 * 
 * @return Embeddings service instance or NULL if initialization failed
 */
embeddings_service_t* embeddings_service_get_instance(void);

/**
 * Check if embeddings service is properly configured
 * 
 * @return 1 if configured, 0 if not
 */
int embeddings_service_is_configured(void);

/**
 * Get embedding vector for text
 * 
 * @param text Text to embed
 * @param embedding Output embedding vector (caller must free with embeddings_free_vector)
 * @return 0 on success, -1 on failure
 */
int embeddings_service_get_vector(const char *text, embedding_vector_t *embedding);

/**
 * Convert text to vector_t for direct use with vector database
 * 
 * @param text Text to embed
 * @return Vector suitable for vector database operations (caller must free data)
 */
vector_t* embeddings_service_text_to_vector(const char *text);

/**
 * Get the embedding dimension used by the service
 *
 * @return Embedding dimension or 0 if service not configured
 */
size_t embeddings_service_get_dimension(void);

/**
 * Free a vector_t created by embeddings_service_text_to_vector
 * 
 * @param vector Vector to free
 */
void embeddings_service_free_vector(vector_t* vector);

/**
 * Force reinitialize embeddings service with current environment variables
 * Useful after loading .env files
 * 
 * @return 0 on success, -1 on failure
 */
int embeddings_service_reinitialize(void);

/**
 * Cleanup embeddings service
 * Should be called on application shutdown
 */
void embeddings_service_cleanup(void);

#endif // EMBEDDINGS_SERVICE_H