#ifndef EMBEDDINGS_SERVICE_H
#define EMBEDDINGS_SERVICE_H

#include "embeddings.h"
#include "db/vector_db.h"

/*
 * Thread-safe singleton that centralizes embedding generation so all modules
 * share a single provider configuration and connection.
 */

typedef struct embeddings_service embeddings_service_t;

embeddings_service_t* embeddings_service_get_instance(void);
int embeddings_service_is_configured(void);

/* Caller owns the returned embedding; free with embeddings_free_vector. */
int embeddings_service_get_vector(const char *text, embedding_vector_t *embedding);

/* Returns a vector_t suitable for direct use with the vector database. Caller must free. */
vector_t* embeddings_service_text_to_vector(const char *text);

size_t embeddings_service_get_dimension(void);
void embeddings_service_free_vector(vector_t* vector);

/* Re-reads env vars. Useful after loading .env files at runtime. */
int embeddings_service_reinitialize(void);

void embeddings_service_cleanup(void);

#endif // EMBEDDINGS_SERVICE_H
