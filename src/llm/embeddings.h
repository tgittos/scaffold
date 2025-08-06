#ifndef EMBEDDINGS_H
#define EMBEDDINGS_H

#include <stddef.h>

// Forward declaration
struct EmbeddingProvider;

typedef struct {
    float *data;
    size_t dimension;
} embedding_vector_t;

typedef struct {
    char *model;
    char *api_key;
    char *api_url;
    struct EmbeddingProvider *provider; // Added provider support
} embeddings_config_t;

/**
 * Initialize embeddings configuration
 * 
 * @param config Pointer to embeddings configuration
 * @param model Model name (e.g., "text-embedding-3-small")
 * @param api_key API key for OpenAI
 * @param api_url API URL (defaults to OpenAI if NULL)
 * @return 0 on success, -1 on failure
 */
int embeddings_init(embeddings_config_t *config, const char *model, 
                    const char *api_key, const char *api_url);

/**
 * Get embedding vector for text
 * 
 * @param config Embeddings configuration
 * @param text Text to embed
 * @param embedding Output embedding vector (caller must free data)
 * @return 0 on success, -1 on failure
 */
int embeddings_get_vector(const embeddings_config_t *config, const char *text, 
                          embedding_vector_t *embedding);

/**
 * Free embedding vector
 * 
 * @param embedding Embedding vector to free
 */
void embeddings_free_vector(embedding_vector_t *embedding);

/**
 * Cleanup embeddings configuration
 * 
 * @param config Configuration to cleanup
 */
void embeddings_cleanup(embeddings_config_t *config);

#endif // EMBEDDINGS_H