#ifndef DOCUMENT_STORE_H
#define DOCUMENT_STORE_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "vector_db.h"
#include "util/ptrarray.h"
#include "util/darray.h"

typedef struct Services Services;

typedef struct {
    size_t id;
    char* content;
    float* embedding;
    size_t embedding_dim;
    time_t timestamp;
    char* type;
    char* source;
    char* metadata_json;
} document_t;

PTRARRAY_DECLARE(DocumentArray, document_t)

typedef struct {
    document_t* document;
    float distance;
} document_result_t;

DARRAY_DECLARE(DocumentResultArray, document_result_t)

typedef struct {
    DocumentResultArray results;
} document_search_results_t;

typedef struct document_store document_store_t;

/** Set the Services container for document store operations. */
void document_store_set_services(Services* services);

document_store_t* document_store_create(const char* base_path);
void document_store_destroy(document_store_t* store);

int document_store_add(document_store_t* store, const char* index_name,
                       const char* content, const float* embedding, 
                       size_t embedding_dim, const char* type,
                       const char* source, const char* metadata_json);

int document_store_add_text(document_store_t* store, const char* index_name,
                           const char* text, const char* type,
                           const char* source, const char* metadata_json);

document_search_results_t* document_store_search(document_store_t* store, 
                                                const char* index_name,
                                                const float* query_embedding,
                                                size_t embedding_dim,
                                                size_t k);

document_search_results_t* document_store_search_text(document_store_t* store,
                                                     const char* index_name,
                                                     const char* query_text,
                                                     size_t k);

document_t* document_store_get(document_store_t* store, const char* index_name, size_t id);

int document_store_update(document_store_t* store, const char* index_name,
                         size_t id, const char* content, const float* embedding,
                         size_t embedding_dim, const char* metadata_json);

int document_store_delete(document_store_t* store, const char* index_name, size_t id);

document_search_results_t* document_store_search_by_time(document_store_t* store,
                                                        const char* index_name,
                                                        time_t start_time,
                                                        time_t end_time,
                                                        size_t limit);

void document_store_free_document(document_t* doc);
void document_store_free_results(document_search_results_t* results);

int document_store_ensure_index(document_store_t* store, const char* index_name,
                               size_t dimension, size_t max_elements);

char** document_store_list_indices(document_store_t* store, size_t* count);

void document_store_clear_conversations(document_store_t* store);

#endif