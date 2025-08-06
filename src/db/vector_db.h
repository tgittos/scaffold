#ifndef VECTOR_DB_H
#define VECTOR_DB_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct vector_db vector_db_t;
typedef struct vector_index vector_index_t;

typedef struct {
    float* data;
    size_t dimension;
} vector_t;

typedef struct {
    size_t label;
    float distance;
} search_result_t;

typedef struct {
    search_result_t* results;
    size_t count;
} search_results_t;

typedef struct {
    size_t dimension;
    size_t max_elements;
    size_t M;                   
    size_t ef_construction;     
    size_t random_seed;
    char* metric;               
} index_config_t;

typedef enum {
    VECTOR_DB_OK = 0,
    VECTOR_DB_ERROR_MEMORY,
    VECTOR_DB_ERROR_INVALID_PARAM,
    VECTOR_DB_ERROR_INDEX_NOT_FOUND,
    VECTOR_DB_ERROR_ELEMENT_NOT_FOUND,
    VECTOR_DB_ERROR_FILE_IO,
    VECTOR_DB_ERROR_SERIALIZATION,
    VECTOR_DB_ERROR_DIMENSION_MISMATCH,
    VECTOR_DB_ERROR_INDEX_FULL
} vector_db_error_t;

vector_db_t* vector_db_create(void);
void vector_db_destroy(vector_db_t* db);

vector_db_error_t vector_db_create_index(vector_db_t* db, const char* index_name, 
                                        const index_config_t* config);
vector_db_error_t vector_db_delete_index(vector_db_t* db, const char* index_name);
bool vector_db_has_index(const vector_db_t* db, const char* index_name);
char** vector_db_list_indices(const vector_db_t* db, size_t* count);

vector_db_error_t vector_db_add_vector(vector_db_t* db, const char* index_name,
                                      const vector_t* vector, size_t label);
vector_db_error_t vector_db_add_vectors(vector_db_t* db, const char* index_name,
                                       const vector_t* vectors, const size_t* labels, 
                                       size_t count);

vector_db_error_t vector_db_update_vector(vector_db_t* db, const char* index_name,
                                         const vector_t* vector, size_t label);

vector_db_error_t vector_db_delete_vector(vector_db_t* db, const char* index_name, size_t label);

vector_db_error_t vector_db_get_vector(const vector_db_t* db, const char* index_name,
                                      size_t label, vector_t* vector);

search_results_t* vector_db_search(const vector_db_t* db, const char* index_name,
                                  const vector_t* query, size_t k);
void vector_db_free_search_results(search_results_t* results);

vector_db_error_t vector_db_save_index(const vector_db_t* db, const char* index_name,
                                      const char* file_path);
vector_db_error_t vector_db_load_index(vector_db_t* db, const char* index_name,
                                      const char* file_path);

vector_db_error_t vector_db_save_all(const vector_db_t* db, const char* directory);
vector_db_error_t vector_db_load_all(vector_db_t* db, const char* directory);

vector_db_error_t vector_db_set_ef_search(vector_db_t* db, const char* index_name, size_t ef);
size_t vector_db_get_index_size(const vector_db_t* db, const char* index_name);
size_t vector_db_get_index_capacity(const vector_db_t* db, const char* index_name);

typedef void (*vector_db_flush_callback_t)(const vector_db_t* db, void* user_data);
vector_db_error_t vector_db_enable_auto_flush(vector_db_t* db, size_t interval_ms,
                                            const char* directory,
                                            vector_db_flush_callback_t callback,
                                            void* user_data);
void vector_db_disable_auto_flush(vector_db_t* db);
vector_db_error_t vector_db_flush_now(vector_db_t* db);

const char* vector_db_error_string(vector_db_error_t error);

char* vector_db_get_default_directory(void);

vector_t* vector_create(size_t dimension);
void vector_destroy(vector_t* vector);
vector_t* vector_clone(const vector_t* vector);

#endif