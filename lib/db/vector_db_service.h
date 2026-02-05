#ifndef VECTOR_DB_SERVICE_H
#define VECTOR_DB_SERVICE_H

#include "vector_db.h"

// Centralizes vector database access so all modules share a single instance.
typedef struct vector_db_service vector_db_service_t;

vector_db_service_t* vector_db_service_create(void);
void vector_db_service_destroy(vector_db_service_t* service);
vector_db_t* vector_db_service_get_database(vector_db_service_t* service);

// Creates the index if it does not already exist.
vector_db_error_t vector_db_service_ensure_index(vector_db_service_t* service, const char* name, const index_config_t* config);

index_config_t vector_db_service_get_memory_config(size_t dimension);

#endif // VECTOR_DB_SERVICE_H