#ifndef VECTOR_DB_SERVICE_H
#define VECTOR_DB_SERVICE_H

#include "vector_db.h"
#include "document_store.h"

typedef struct Services Services;

// Centralizes vector database access so all modules share a single instance.
typedef struct vector_db_service vector_db_service_t;

vector_db_service_t* vector_db_service_create(void);
void vector_db_service_destroy(vector_db_service_t* service);
vector_db_t* vector_db_service_get_database(vector_db_service_t* service);

// Creates the index if it does not already exist.
vector_db_error_t vector_db_service_ensure_index(vector_db_service_t* service, const char* name, const index_config_t* config);

/**
 * Add text to the document store after computing its embedding.
 * Gets document_store from Services and wraps document_store_add() with automatic embedding generation.
 */
int vector_db_service_add_text(Services* services, const char* index_name,
                               const char* text, const char* type,
                               const char* source, const char* metadata_json);

/**
 * Search the document store by text query.
 * Gets document_store from Services, computes the embedding, and delegates to document_store_search().
 */
document_search_results_t* vector_db_service_search_text(Services* services,
                                                         const char* index_name,
                                                         const char* query_text,
                                                         size_t k);

char** vector_db_service_list_indices(vector_db_service_t* service, size_t* count);
size_t vector_db_service_get_index_size(vector_db_service_t* service, const char* index_name);
size_t vector_db_service_get_index_capacity(vector_db_service_t* service, const char* index_name);
bool vector_db_service_has_index(vector_db_service_t* service, const char* index_name);
vector_db_error_t vector_db_service_update_vector(vector_db_service_t* service, const char* index_name,
                                                   const vector_t* vector, size_t label);

#endif // VECTOR_DB_SERVICE_H
