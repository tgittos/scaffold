#ifndef HNSWLIB_WRAPPER_H
#define HNSWLIB_WRAPPER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* hnswlib_index_t;

typedef struct {
    size_t dimension;
    size_t max_elements;
    size_t M;
    size_t ef_construction;
    size_t random_seed;
    const char* metric;
} hnswlib_index_config_t;

typedef struct {
    size_t* labels;
    float* distances;
    size_t count;
} hnswlib_search_results_t;

hnswlib_index_t hnswlib_create_index(const char* name, const hnswlib_index_config_t* config);
int hnswlib_delete_index(const char* name);

int hnswlib_add_vector(const char* name, const float* data, size_t label);
int hnswlib_update_vector(const char* name, const float* data, size_t label);
int hnswlib_delete_vector(const char* name, size_t label);
int hnswlib_get_vector(const char* name, size_t label, float* data);

hnswlib_search_results_t* hnswlib_search(const char* name, const float* query, size_t k);
void hnswlib_free_search_results(hnswlib_search_results_t* results);

int hnswlib_save_index(const char* name, const char* path);
int hnswlib_load_index(const char* name, const char* path, const hnswlib_index_config_t* config);

int hnswlib_set_ef(const char* name, size_t ef);
size_t hnswlib_get_current_count(const char* name);

#ifdef __cplusplus
}
#endif

#endif