#include "document_store.h"
#include "vector_db_service.h"
#include "hnswlib_wrapper.h"
#include "../util/ralph_home.h"
#include "llm/embeddings_service.h"
#include "../services/services.h"
#include "util/ptrarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cJSON.h>

PTRARRAY_DEFINE(DocumentArray, document_t)
DARRAY_DEFINE(DocumentResultArray, document_result_t)

struct document_store {
    char* base_path;
    vector_db_t* vector_db;
};

static Services* g_services = NULL;

void document_store_set_services(Services* services) {
    g_services = services;
}

static int rmdir_recursive(const char* path) {
    DIR* dir = opendir(path);
    if (dir == NULL) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char full_path[1024] = {0};
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                rmdir_recursive(full_path);
            } else {
                unlink(full_path);
            }
        }
    }
    closedir(dir);
    return rmdir(path);
}

static char* get_document_path(document_store_t* store, const char* index_name) {
    if (store == NULL || index_name == NULL) return NULL;
    
    size_t path_len = strlen(store->base_path) + strlen(index_name) + 64;
    char* path = malloc(path_len);
    if (path == NULL) return NULL;
    
    snprintf(path, path_len, "%s/documents/%s", store->base_path, index_name);
    
    mkdir(store->base_path, 0755);
    char docs_path[512];
    snprintf(docs_path, sizeof(docs_path), "%s/documents", store->base_path);
    mkdir(docs_path, 0755);
    mkdir(path, 0755);
    
    return path;
}

static char* get_document_filename(const char* index_path, size_t id) {
    size_t path_len = strlen(index_path) + 64;
    char* filename = malloc(path_len);
    if (filename == NULL) return NULL;
    
    snprintf(filename, path_len, "%s/doc_%zu.json", index_path, id);
    return filename;
}

document_store_t* document_store_create(const char* base_path) {
    document_store_t* store = calloc(1, sizeof(document_store_t));
    if (store == NULL) return NULL;

    if (base_path == NULL) {
        const char* home = ralph_home_get();
        if (home == NULL) {
            free(store);
            return NULL;
        }
        store->base_path = strdup(home);
        if (store->base_path == NULL) {
            free(store);
            return NULL;
        }
    } else {
        store->base_path = strdup(base_path);
        if (store->base_path == NULL) {
            free(store);
            return NULL;
        }
    }

    vector_db_service_t* vdb_service = services_get_vector_db(g_services);
    store->vector_db = vector_db_service_get_database(vdb_service);

    return store;
}

void document_store_destroy(document_store_t* store) {
    if (store == NULL) return;
    free(store->base_path);
    free(store);
}

void document_store_clear_conversations(document_store_t* store) {
    if (store == NULL) return;

    char conv_path[512] = {0};
    if (store->base_path != NULL) {
        snprintf(conv_path, sizeof(conv_path), "%s/documents/conversations", store->base_path);
        rmdir_recursive(conv_path);
    }

    if (store->vector_db != NULL) {
        vector_db_delete_index(store->vector_db, "conversations");
    }
}

static int save_document(document_store_t* store, const char* index_name, size_t id,
                        const char* content, const char* type, const char* source,
                        const char* metadata_json, time_t timestamp) {
    char* index_path = get_document_path(store, index_name);
    if (index_path == NULL) return -1;
    
    char* filename = get_document_filename(index_path, id);
    if (filename == NULL) {
        free(index_path);
        return -1;
    }
    
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        free(filename);
        free(index_path);
        return -1;
    }
    
    cJSON_AddNumberToObject(json, "id", id);
    cJSON_AddStringToObject(json, "content", content ? content : "");
    cJSON_AddStringToObject(json, "type", type ? type : "text");
    cJSON_AddStringToObject(json, "source", source ? source : "api");
    cJSON_AddNumberToObject(json, "timestamp", timestamp);
    
    if (metadata_json) {
        cJSON* metadata = cJSON_Parse(metadata_json);
        if (metadata) {
            cJSON_AddItemToObject(json, "metadata", metadata);
        }
    }
    
    char* json_str = cJSON_Print(json);
    if (json_str == NULL) {
        cJSON_Delete(json);
        free(filename);
        free(index_path);
        return -1;
    }
    
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        free(json_str);
        cJSON_Delete(json);
        free(filename);
        free(index_path);
        return -1;
    }
    
    fprintf(file, "%s", json_str);
    fclose(file);
    
    free(json_str);
    cJSON_Delete(json);
    free(filename);
    free(index_path);
    
    return 0;
}

static document_t* load_document(document_store_t* store, const char* index_name, size_t id) {
    char* index_path = get_document_path(store, index_name);
    if (index_path == NULL) return NULL;
    
    char* filename = get_document_filename(index_path, id);
    if (filename == NULL) {
        free(index_path);
        return NULL;
    }
    
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        free(filename);
        free(index_path);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* buffer = malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        free(filename);
        free(index_path);
        return NULL;
    }
    
    size_t read_size = fread(buffer, 1, file_size, file);
    buffer[read_size] = '\0';
    fclose(file);
    
    cJSON* json = cJSON_Parse(buffer);
    free(buffer);
    
    if (json == NULL) {
        free(filename);
        free(index_path);
        return NULL;
    }
    
    document_t* doc = calloc(1, sizeof(document_t));
    if (doc == NULL) {
        cJSON_Delete(json);
        free(filename);
        free(index_path);
        return NULL;
    }
    
    cJSON* item = cJSON_GetObjectItem(json, "id");
    if (item) doc->id = (size_t)item->valuedouble;

    item = cJSON_GetObjectItem(json, "content");
    if (item && item->valuestring) {
        doc->content = strdup(item->valuestring);
        if (!doc->content) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "type");
    if (item && item->valuestring) {
        doc->type = strdup(item->valuestring);
        if (!doc->type) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "source");
    if (item && item->valuestring) {
        doc->source = strdup(item->valuestring);
        if (!doc->source) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "timestamp");
    if (item) doc->timestamp = (time_t)item->valuedouble;

    item = cJSON_GetObjectItem(json, "metadata");
    if (item) {
        doc->metadata_json = cJSON_Print(item);
        if (!doc->metadata_json) goto alloc_error;
    }

    cJSON_Delete(json);
    free(filename);
    free(index_path);

    return doc;

alloc_error:
    document_store_free_document(doc);
    cJSON_Delete(json);
    free(filename);
    free(index_path);
    return NULL;
}

int document_store_add(document_store_t* store, const char* index_name,
                      const char* content, const float* embedding, 
                      size_t embedding_dim, const char* type,
                      const char* source, const char* metadata_json) {
    if (store == NULL || index_name == NULL || embedding == NULL) return -1;
    
    size_t id = vector_db_get_index_size(store->vector_db, index_name);
    
    vector_t vec = {
        .data = (float*)embedding,
        .dimension = embedding_dim
    };
    
    vector_db_error_t err = vector_db_add_vector(store->vector_db, index_name, &vec, id);
    if (err != VECTOR_DB_OK) {
        return -1;
    }
    
    time_t timestamp = time(NULL);
    if (save_document(store, index_name, id, content, type, source, metadata_json, timestamp) != 0) {
        vector_db_delete_vector(store->vector_db, index_name, id);
        return -1;
    }
    
    return 0;
}

int document_store_add_text(document_store_t* store, const char* index_name,
                           const char* text, const char* type,
                           const char* source, const char* metadata_json) {
    if (store == NULL || index_name == NULL || text == NULL) return -1;

    embeddings_service_t* emb = services_get_embeddings(g_services);
    if (!embeddings_service_is_configured(emb)) {
        size_t fallback_dim = 1536;
        float *zero_embedding = calloc(fallback_dim, sizeof(float));
        if (zero_embedding == NULL) return -1;

        int result = document_store_add(store, index_name, text, zero_embedding,
                                       fallback_dim, type, source, metadata_json);
        free(zero_embedding);
        return result;
    }

    embedding_vector_t embedding;
    if (embeddings_service_get_vector(emb, text, &embedding) != 0) {
        return -1;
    }

    int result = document_store_add(store, index_name, text, embedding.data,
                                   embedding.dimension, type, source, metadata_json);

    embeddings_service_free_embedding(&embedding);
    return result;
}

document_search_results_t* document_store_search(document_store_t* store, 
                                               const char* index_name,
                                               const float* query_embedding,
                                               size_t embedding_dim,
                                               size_t k) {
    if (store == NULL || index_name == NULL || query_embedding == NULL) return NULL;
    
    vector_t query = {
        .data = (float*)query_embedding,
        .dimension = embedding_dim
    };
    
    search_results_t* vector_results = vector_db_search(store->vector_db, index_name, &query, k);
    if (vector_results == NULL) return NULL;
    
    document_search_results_t* results = calloc(1, sizeof(document_search_results_t));
    if (results == NULL) {
        vector_db_free_search_results(vector_results);
        return NULL;
    }

    if (DocumentResultArray_init_capacity(&results->results, vector_results->count) != 0) {
        free(results);
        vector_db_free_search_results(vector_results);
        return NULL;
    }

    for (size_t i = 0; i < vector_results->count; i++) {
        document_result_t result;
        result.document = load_document(store, index_name, vector_results->results[i].label);
        result.distance = vector_results->results[i].distance;

        if (result.document) {
            vector_t vec;
            vec.data = malloc(embedding_dim * sizeof(float));
            vec.dimension = embedding_dim;

            if (vec.data && vector_db_get_vector(store->vector_db, index_name,
                                                vector_results->results[i].label, &vec) == VECTOR_DB_OK) {
                result.document->embedding = vec.data;
                result.document->embedding_dim = vec.dimension;
            } else {
                free(vec.data);
            }
        }

        if (DocumentResultArray_push(&results->results, result) != 0) {
            document_store_free_document(result.document);
        }
    }

    vector_db_free_search_results(vector_results);
    return results;
}

document_search_results_t* document_store_search_text(document_store_t* store,
                                                     const char* index_name,
                                                     const char* query_text,
                                                     size_t k) {
    if (store == NULL || index_name == NULL || query_text == NULL) return NULL;

    embeddings_service_t* emb = services_get_embeddings(g_services);
    if (!embeddings_service_is_configured(emb)) {
        return NULL;
    }

    embedding_vector_t embedding;
    if (embeddings_service_get_vector(emb, query_text, &embedding) != 0) {
        return NULL;
    }

    document_search_results_t* results = document_store_search(store, index_name,
                                                              embedding.data, embedding.dimension, k);

    embeddings_service_free_embedding(&embedding);
    return results;
}

document_t* document_store_get(document_store_t* store, const char* index_name, size_t id) {
    if (store == NULL || index_name == NULL) return NULL;
    
    document_t* doc = load_document(store, index_name, id);
    if (doc == NULL) return NULL;
    
    vector_t vec;
    vec.data = NULL;
    vec.dimension = 0;

    if (vector_db_get_vector(store->vector_db, index_name, id, &vec) == VECTOR_DB_OK) {
        doc->embedding = vec.data;
        doc->embedding_dim = vec.dimension;
    } else {
        free(vec.data);
    }
    
    return doc;
}

int document_store_update(document_store_t* store, const char* index_name,
                         size_t id, const char* content, const float* embedding,
                         size_t embedding_dim, const char* metadata_json) {
    if (store == NULL || index_name == NULL) return -1;
    
    document_t* existing = document_store_get(store, index_name, id);
    if (existing == NULL) return -1;
    
    if (embedding != NULL) {
        vector_t vec = {
            .data = (float*)embedding,
            .dimension = embedding_dim
        };
        
        vector_db_error_t err = vector_db_update_vector(store->vector_db, index_name, &vec, id);
        if (err != VECTOR_DB_OK) {
            document_store_free_document(existing);
            return -1;
        }
    }
    
    if (save_document(store, index_name, id, 
                     content ? content : existing->content,
                     existing->type, existing->source,
                     metadata_json ? metadata_json : existing->metadata_json,
                     time(NULL)) != 0) {
        document_store_free_document(existing);
        return -1;
    }
    
    document_store_free_document(existing);
    return 0;
}

int document_store_delete(document_store_t* store, const char* index_name, size_t id) {
    if (store == NULL || index_name == NULL) return -1;
    
    vector_db_error_t err = vector_db_delete_vector(store->vector_db, index_name, id);
    if (err != VECTOR_DB_OK) {
        return -1;
    }
    
    char* index_path = get_document_path(store, index_name);
    if (index_path == NULL) return -1;
    
    char* filename = get_document_filename(index_path, id);
    if (filename == NULL) {
        free(index_path);
        return -1;
    }
    
    remove(filename);
    
    free(filename);
    free(index_path);
    
    return 0;
}

document_search_results_t* document_store_search_by_time(document_store_t* store,
                                                        const char* index_name,
                                                        time_t start_time,
                                                        time_t end_time,
                                                        size_t limit) {
    if (store == NULL || index_name == NULL) return NULL;

    char* index_path = get_document_path(store, index_name);
    if (index_path == NULL) return NULL;

    DIR* dir = opendir(index_path);
    if (dir == NULL) {
        free(index_path);
        return NULL;
    }

    DocumentArray doc_arr;
    size_t initial_capacity = limit > 0 ? limit : 100;
    if (DocumentArray_init_capacity(&doc_arr, initial_capacity, NULL) != 0) {
        closedir(dir);
        free(index_path);
        return NULL;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL && (limit == 0 || doc_arr.count < limit)) {
        if (strncmp(entry->d_name, "doc_", 4) == 0) {
            size_t id;
            if (sscanf(entry->d_name, "doc_%zu.json", &id) == 1) {
                document_t* doc = load_document(store, index_name, id);
                if (doc && doc->timestamp >= start_time && doc->timestamp <= end_time) {
                    if (DocumentArray_push(&doc_arr, doc) != 0) {
                        document_store_free_document(doc);
                        break;
                    }
                } else if (doc) {
                    document_store_free_document(doc);
                }
            }
        }
    }

    closedir(dir);
    free(index_path);

    if (doc_arr.count == 0) {
        DocumentArray_destroy_shallow(&doc_arr);
        return NULL;
    }

    document_search_results_t* results = calloc(1, sizeof(document_search_results_t));
    if (results == NULL) {
        DocumentArray_destroy(&doc_arr);
        return NULL;
    }

    if (DocumentResultArray_init_capacity(&results->results, doc_arr.count) != 0) {
        DocumentArray_destroy(&doc_arr);
        free(results);
        return NULL;
    }

    for (size_t i = 0; i < doc_arr.count; i++) {
        // Distance is meaningless for time-based searches, so zero is used as a sentinel.
        document_result_t result = {
            .document = doc_arr.data[i],
            .distance = 0.0f
        };
        if (DocumentResultArray_push(&results->results, result) != 0) {
            document_store_free_document(doc_arr.data[i]);
        }
    }

    DocumentArray_destroy_shallow(&doc_arr);

    return results;
}

void document_store_free_document(document_t* doc) {
    if (doc == NULL) return;
    
    free(doc->content);
    free(doc->embedding);
    free(doc->type);
    free(doc->source);
    free(doc->metadata_json);
    free(doc);
}

void document_store_free_results(document_search_results_t* results) {
    if (results == NULL) return;

    for (size_t i = 0; i < results->results.count; i++) {
        document_store_free_document(results->results.data[i].document);
    }

    DocumentResultArray_destroy(&results->results);
    free(results);
}

int document_store_ensure_index(document_store_t* store, const char* index_name,
                               size_t dimension, size_t max_elements) {
    if (store == NULL || index_name == NULL) return -1;
    
    index_config_t config = {
        .dimension = dimension,
        .max_elements = max_elements > 0 ? max_elements : 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 42,
        .metric = "cosine"
    };
    
    vector_db_service_t* vdb_service = services_get_vector_db(g_services);
    return vector_db_service_ensure_index(vdb_service, index_name, &config);
}

char** document_store_list_indices(document_store_t* store, size_t* count) {
    if (store == NULL || count == NULL) return NULL;
    
    return vector_db_list_indices(store->vector_db, count);
}