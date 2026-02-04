#ifndef EMBEDDINGS_H
#define EMBEDDINGS_H

#include <stddef.h>

struct EmbeddingProvider;

typedef struct {
    float *data;
    size_t dimension;
} embedding_vector_t;

typedef struct {
    char *model;
    char *api_key;
    char *api_url;
    struct EmbeddingProvider *provider;
} embeddings_config_t;

/* Defaults api_url to OpenAI endpoint when NULL. Detects provider from URL. */
int embeddings_init(embeddings_config_t *config, const char *model,
                    const char *api_key, const char *api_url);

/* Caller owns the returned embedding and must free it via embeddings_free_vector. */
int embeddings_get_vector(const embeddings_config_t *config, const char *text,
                          embedding_vector_t *embedding);

void embeddings_free_vector(embedding_vector_t *embedding);
void embeddings_cleanup(embeddings_config_t *config);

#endif // EMBEDDINGS_H
