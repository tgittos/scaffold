#ifndef EMBEDDINGS_SERVICE_H
#define EMBEDDINGS_SERVICE_H

#include "embeddings.h"

/*
 * Dimension used when no working embedding model is available (degraded
 * mode): stored vectors fall back to zero embeddings of this size.
 */
#define EMBEDDINGS_DIM_DEFAULT 1536

/* Forward declaration to break LLM -> DB layer dependency */
typedef struct vector_t vector_t;

/*
 * Centralizes embedding generation so all modules share a single provider
 * configuration and connection.
 */

typedef struct embeddings_service embeddings_service_t;

embeddings_service_t* embeddings_service_create(void);
void embeddings_service_destroy(embeddings_service_t* service);
int embeddings_service_is_configured(embeddings_service_t* service);

/* Caller owns the returned embedding; free with embeddings_service_free_embedding. */
int embeddings_service_get_vector(embeddings_service_t* service, const char *text, embedding_vector_t *embedding);

/* Free an embedding returned by embeddings_service_get_vector. */
void embeddings_service_free_embedding(embedding_vector_t *embedding);

/* Returns a vector_t suitable for direct use with the vector database. Caller must free. */
vector_t* embeddings_service_text_to_vector(embeddings_service_t* service, const char *text);

/*
 * Dimension to use when creating vector indexes: the model's actual
 * dimension, resolved by probing the embedding endpoint, or
 * EMBEDDINGS_DIM_DEFAULT when the feature is disabled.
 */
size_t embeddings_service_get_dimension(embeddings_service_t* service);
void embeddings_service_free_vector(vector_t* vector);

/* Re-reads env vars and re-probes the model. Always returns 0 (except
 * for a NULL service): a failed probe disables the embedding feature
 * instead of failing, so check is_configured()/get_dimension() after. */
int embeddings_service_reinitialize(embeddings_service_t* service);

#endif // EMBEDDINGS_SERVICE_H
