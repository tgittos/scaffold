#include "vector_db.h"
#include "hnswlib_wrapper.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <pwd.h>

typedef struct index_entry {
    char* name;
    void* index;  
    index_config_t config;
    pthread_rwlock_t lock;
    struct index_entry* next;
} index_entry_t;

struct vector_db {
    index_entry_t* indices;
    pthread_mutex_t mutex;
    
    pthread_t flush_thread;
    bool flush_enabled;
    size_t flush_interval_ms;
    char* flush_directory;
    vector_db_flush_callback_t flush_callback;
    void* flush_user_data;
    pthread_mutex_t flush_mutex;
    pthread_cond_t flush_cond;
};

static void* flush_thread_func(void* arg);

char* vector_db_get_default_directory(void) {
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home = pw->pw_dir;
        }
    }
    
    if (!home) {
        return NULL;
    }
    
    char* path = malloc(strlen(home) + strlen("/.local/ralph") + 1);
    if (!path) {
        return NULL;
    }
    
    sprintf(path, "%s/.local/ralph", home);
    
    struct stat st;
    if (stat(path, &st) != 0) {
        char local_path[strlen(home) + strlen("/.local") + 1];
        sprintf(local_path, "%s/.local", home);
        
        if (stat(local_path, &st) != 0) {
            if (mkdir(local_path, 0755) != 0) {
                free(path);
                return NULL;
            }
        }
        
        if (mkdir(path, 0755) != 0) {
            free(path);
            return NULL;
        }
    }
    
    return path;
}

static index_entry_t* find_index(const vector_db_t* db, const char* index_name) {
    index_entry_t* entry = db->indices;
    while (entry) {
        if (strcmp(entry->name, index_name) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

vector_db_t* vector_db_create(void) {
    vector_db_t* db = calloc(1, sizeof(vector_db_t));
    if (!db) return NULL;
    
    if (pthread_mutex_init(&db->mutex, NULL) != 0) {
        free(db);
        return NULL;
    }
    
    if (pthread_mutex_init(&db->flush_mutex, NULL) != 0) {
        pthread_mutex_destroy(&db->mutex);
        free(db);
        return NULL;
    }
    
    if (pthread_cond_init(&db->flush_cond, NULL) != 0) {
        pthread_mutex_destroy(&db->mutex);
        pthread_mutex_destroy(&db->flush_mutex);
        free(db);
        return NULL;
    }
    
    char* default_dir = vector_db_get_default_directory();
    if (default_dir) {
        vector_db_enable_auto_flush(db, 100, default_dir, NULL, NULL); // 100ms auto-flush
        free(default_dir);
    }
    
    return db;
}

void vector_db_destroy(vector_db_t* db) {
    if (!db) return;
    
    vector_db_disable_auto_flush(db);
    
    pthread_mutex_lock(&db->mutex);
    
    index_entry_t* entry = db->indices;
    while (entry) {
        index_entry_t* next = entry->next;
        
        // Delete the index from hnswlib
        hnswlib_delete_index(entry->name);
        
        free(entry->name);
        free(entry->config.metric);
        pthread_rwlock_destroy(&entry->lock);
        free(entry);
        
        entry = next;
    }
    
    pthread_mutex_unlock(&db->mutex);
    
    pthread_mutex_destroy(&db->mutex);
    pthread_mutex_destroy(&db->flush_mutex);
    pthread_cond_destroy(&db->flush_cond);
    free(db->flush_directory);
    free(db);
}

vector_db_error_t vector_db_create_index(vector_db_t* db, const char* index_name, 
                                        const index_config_t* config) {
    if (!db || !index_name || !config) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    if (config->dimension == 0 || config->max_elements == 0) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    
    if (find_index(db, index_name)) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    index_entry_t* entry = calloc(1, sizeof(index_entry_t));
    if (!entry) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    entry->name = strdup(index_name);
    if (!entry->name) {
        free(entry);
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    entry->config = *config;
    if (config->metric) {
        entry->config.metric = strdup(config->metric);
        if (!entry->config.metric) {
            free(entry->name);
            free(entry);
            pthread_mutex_unlock(&db->mutex);
            return VECTOR_DB_ERROR_MEMORY;
        }
    } else {
        entry->config.metric = strdup("l2");
    }
    
    if (pthread_rwlock_init(&entry->lock, NULL) != 0) {
        free(entry->config.metric);
        free(entry->name);
        free(entry);
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    hnswlib_index_config_t hnsw_config = {
        .dimension = config->dimension,
        .max_elements = config->max_elements,
        .M = config->M ? config->M : 16,
        .ef_construction = config->ef_construction ? config->ef_construction : 200,
        .random_seed = config->random_seed ? config->random_seed : 100,
        .metric = entry->config.metric
    };
    
    if (hnswlib_create_index(index_name, &hnsw_config) == NULL) {
        pthread_rwlock_destroy(&entry->lock);
        free(entry->config.metric);
        free(entry->name);
        free(entry);
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    entry->next = db->indices;
    db->indices = entry;
    
    pthread_mutex_unlock(&db->mutex);
    return VECTOR_DB_OK;
}

vector_db_error_t vector_db_delete_index(vector_db_t* db, const char* index_name) {
    if (!db || !index_name) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    
    index_entry_t* prev = NULL;
    index_entry_t* entry = db->indices;
    
    while (entry) {
        if (strcmp(entry->name, index_name) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                db->indices = entry->next;
            }
            
            pthread_rwlock_wrlock(&entry->lock);
            hnswlib_delete_index(entry->name);
            free(entry->name);
            free(entry->config.metric);
            pthread_rwlock_unlock(&entry->lock);
            pthread_rwlock_destroy(&entry->lock);
            free(entry);
            
            pthread_mutex_unlock(&db->mutex);
            return VECTOR_DB_OK;
        }
        prev = entry;
        entry = entry->next;
    }
    
    pthread_mutex_unlock(&db->mutex);
    return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
}

bool vector_db_has_index(const vector_db_t* db, const char* index_name) {
    if (!db || !index_name) return false;
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    bool found = find_index(db, index_name) != NULL;
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    
    return found;
}

char** vector_db_list_indices(const vector_db_t* db, size_t* count) {
    if (!db || !count) return NULL;
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    
    *count = 0;
    index_entry_t* entry = db->indices;
    while (entry) {
        (*count)++;
        entry = entry->next;
    }
    
    if (*count == 0) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return NULL;
    }
    
    char** names = calloc(*count, sizeof(char*));
    if (!names) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        *count = 0;
        return NULL;
    }
    
    entry = db->indices;
    for (size_t i = 0; i < *count && entry; i++) {
        names[i] = strdup(entry->name);
        if (!names[i]) {
            for (size_t j = 0; j < i; j++) {
                free(names[j]);
            }
            free(names);
            pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
            *count = 0;
            return NULL;
        }
        entry = entry->next;
    }
    
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    return names;
}

vector_db_error_t vector_db_add_vector(vector_db_t* db, const char* index_name,
                                      const vector_t* vector, size_t label) {
    if (!db || !index_name || !vector || !vector->data) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
    }
    
    if (vector->dimension != entry->config.dimension) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_DIMENSION_MISMATCH;
    }
    
    pthread_rwlock_wrlock(&entry->lock);
    pthread_mutex_unlock(&db->mutex);
    
    int result = hnswlib_add_vector(index_name, vector->data, label);
    
    pthread_rwlock_unlock(&entry->lock);
    return (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_MEMORY;
}

vector_db_error_t vector_db_add_vectors(vector_db_t* db, const char* index_name,
                                       const vector_t* vectors, const size_t* labels, 
                                       size_t count) {
    if (!db || !index_name || !vectors || !labels || count == 0) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    for (size_t i = 0; i < count; i++) {
        vector_db_error_t err = vector_db_add_vector(db, index_name, &vectors[i], labels[i]);
        if (err != VECTOR_DB_OK) {
            return err;
        }
    }
    
    return VECTOR_DB_OK;
}

vector_db_error_t vector_db_update_vector(vector_db_t* db, const char* index_name,
                                         const vector_t* vector, size_t label) {
    if (!db || !index_name || !vector || !vector->data) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
    }
    
    if (vector->dimension != entry->config.dimension) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_DIMENSION_MISMATCH;
    }
    
    pthread_rwlock_wrlock(&entry->lock);
    pthread_mutex_unlock(&db->mutex);
    
    int result = hnswlib_update_vector(index_name, vector->data, label);
    
    pthread_rwlock_unlock(&entry->lock);
    return (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_ELEMENT_NOT_FOUND;
}

vector_db_error_t vector_db_delete_vector(vector_db_t* db, const char* index_name, size_t label) {
    if (!db || !index_name) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
    }
    
    pthread_rwlock_wrlock(&entry->lock);
    pthread_mutex_unlock(&db->mutex);
    
    int result = hnswlib_delete_vector(index_name, label);
    
    pthread_rwlock_unlock(&entry->lock);
    return (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_ELEMENT_NOT_FOUND;
}

vector_db_error_t vector_db_get_vector(const vector_db_t* db, const char* index_name,
                                      size_t label, vector_t* vector) {
    if (!db || !index_name || !vector) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
    }
    
    if (!vector->data || vector->dimension != entry->config.dimension) {
        vector->dimension = entry->config.dimension;
        vector->data = realloc(vector->data, vector->dimension * sizeof(float));
        if (!vector->data) {
            pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
            return VECTOR_DB_ERROR_MEMORY;
        }
    }
    
    pthread_rwlock_rdlock(&entry->lock);
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    
    int result = hnswlib_get_vector(index_name, label, vector->data);
    
    pthread_rwlock_unlock(&entry->lock);
    return (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_ELEMENT_NOT_FOUND;
}

search_results_t* vector_db_search(const vector_db_t* db, const char* index_name,
                                  const vector_t* query, size_t k) {
    if (!db || !index_name || !query || !query->data || k == 0) {
        return NULL;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return NULL;
    }
    
    if (query->dimension != entry->config.dimension) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return NULL;
    }
    
    pthread_rwlock_rdlock(&entry->lock);
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    
    hnswlib_search_results_t* hnsw_results = hnswlib_search(index_name, query->data, k);
    pthread_rwlock_unlock(&entry->lock);
    
    if (!hnsw_results) {
        return NULL;
    }
    
    search_results_t* results = calloc(1, sizeof(search_results_t));
    if (!results) {
        hnswlib_free_search_results(hnsw_results);
        return NULL;
    }
    
    results->count = hnsw_results->count;
    results->results = calloc(results->count, sizeof(search_result_t));
    if (!results->results) {
        free(results);
        hnswlib_free_search_results(hnsw_results);
        return NULL;
    }
    
    for (size_t i = 0; i < results->count; i++) {
        results->results[i].label = hnsw_results->labels[i];
        results->results[i].distance = hnsw_results->distances[i];
    }
    
    hnswlib_free_search_results(hnsw_results);
    return results;
}

void vector_db_free_search_results(search_results_t* results) {
    if (!results) return;
    free(results->results);
    free(results);
}

vector_db_error_t vector_db_save_index(const vector_db_t* db, const char* index_name,
                                      const char* file_path) {
    if (!db || !index_name || !file_path) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
    }
    
    pthread_rwlock_rdlock(&entry->lock);
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    
    int result = hnswlib_save_index(index_name, file_path);
    
    if (result == 0) {
        char meta_path[4096] = {0};
        snprintf(meta_path, sizeof(meta_path), "%s.meta", file_path);
        FILE* meta = fopen(meta_path, "w");
        if (meta) {
            fprintf(meta, "%zu %zu\n", entry->config.dimension, entry->config.max_elements);
            fprintf(meta, "%s\n", entry->config.metric ? entry->config.metric : "l2");
            fclose(meta);
        }
    }
    
    pthread_rwlock_unlock(&entry->lock);
    return (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_FILE_IO;
}

vector_db_error_t vector_db_load_index(vector_db_t* db, const char* index_name,
                                      const char* file_path) {
    if (!db || !index_name || !file_path) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    
    if (find_index(db, index_name)) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    // Read metadata first to get config
    char meta_path[4096] = {0};
    snprintf(meta_path, sizeof(meta_path), "%s.meta", file_path);
    FILE* meta = fopen(meta_path, "r");
    
    hnswlib_index_config_t hnswlib_config;
    char metric[64] = "l2";
    
    if (meta) {
        size_t dimension, max_elements;
        if (fscanf(meta, "%zu %zu", &dimension, &max_elements) == 2) {
            fscanf(meta, "%s", metric);
            hnswlib_config.dimension = dimension;
            hnswlib_config.max_elements = max_elements;
        } else {
            fclose(meta);
            pthread_mutex_unlock(&db->mutex);
            return VECTOR_DB_ERROR_FILE_IO;
        }
        fclose(meta);
    } else {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_FILE_IO;
    }
    
    hnswlib_config.metric = metric;
    hnswlib_config.M = 16;
    hnswlib_config.ef_construction = 200;
    hnswlib_config.random_seed = 100;
    
    int result = hnswlib_load_index(index_name, file_path, &hnswlib_config);
    if (result != 0) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_FILE_IO;
    }
    
    index_entry_t* entry = calloc(1, sizeof(index_entry_t));
    if (!entry) {
        hnswlib_delete_index(index_name);
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    entry->name = strdup(index_name);
    if (!entry->name) {
        free(entry);
        hnswlib_delete_index(index_name);
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    // Copy config from what we read
    entry->config.dimension = hnswlib_config.dimension;
    entry->config.max_elements = hnswlib_config.max_elements;
    entry->config.metric = strdup(metric);
    entry->config.M = hnswlib_config.M;
    entry->config.ef_construction = hnswlib_config.ef_construction;
    entry->config.random_seed = hnswlib_config.random_seed;
    
    if (pthread_rwlock_init(&entry->lock, NULL) != 0) {
        free(entry->config.metric);
        free(entry->name);
        free(entry);
        hnswlib_delete_index(index_name);
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    entry->next = db->indices;
    db->indices = entry;
    
    pthread_mutex_unlock(&db->mutex);
    return VECTOR_DB_OK;
}

vector_db_error_t vector_db_save_all(const vector_db_t* db, const char* directory) {
    if (!db || !directory) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    struct stat st;
    if (stat(directory, &st) != 0) {
        if (mkdir(directory, 0755) != 0) {
            return VECTOR_DB_ERROR_FILE_IO;
        }
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    
    index_entry_t* entry = db->indices;
    while (entry) {
        char file_path[4096] = {0};
        snprintf(file_path, sizeof(file_path), "%s/%s.index", directory, entry->name);
        
        pthread_rwlock_rdlock(&entry->lock);
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        
        int result = hnswlib_save_index(entry->name, file_path);
        
        if (result == 0) {
            char meta_path[8192] = {0};
            snprintf(meta_path, sizeof(meta_path), "%s.meta", file_path);
            FILE* meta = fopen(meta_path, "w");
            if (meta) {
                fprintf(meta, "%zu %zu\n", entry->config.dimension, entry->config.max_elements);
                fprintf(meta, "%s\n", entry->config.metric ? entry->config.metric : "l2");
                fclose(meta);
            }
        }
        
        pthread_rwlock_unlock(&entry->lock);
        pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
        
        vector_db_error_t err = (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_FILE_IO;
        
        if (err != VECTOR_DB_OK) {
            pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
            return err;
        }
        
        entry = entry->next;
    }
    
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    return VECTOR_DB_OK;
}

vector_db_error_t vector_db_load_all(vector_db_t* db, const char* directory) {
    if (!db || !directory) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    DIR* dir = opendir(directory);
    if (!dir) {
        return VECTOR_DB_ERROR_FILE_IO;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len > 6 && strcmp(entry->d_name + len - 6, ".index") == 0) {
            char index_name[256] = {0};
            memcpy(index_name, entry->d_name, len - 6);
            index_name[len - 6] = '\0';
            
            char file_path[4096] = {0};
            snprintf(file_path, sizeof(file_path), "%s/%s", directory, entry->d_name);
            
            vector_db_error_t err = vector_db_load_index(db, index_name, file_path);
            if (err != VECTOR_DB_OK) {
                closedir(dir);
                return err;
            }
        }
    }
    
    closedir(dir);
    return VECTOR_DB_OK;
}

vector_db_error_t vector_db_set_ef_search(vector_db_t* db, const char* index_name, size_t ef) {
    if (!db || !index_name || ef == 0) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock(&db->mutex);
        return VECTOR_DB_ERROR_INDEX_NOT_FOUND;
    }
    
    pthread_rwlock_wrlock(&entry->lock);
    pthread_mutex_unlock(&db->mutex);
    
    int result = hnswlib_set_ef(index_name, ef);
    
    pthread_rwlock_unlock(&entry->lock);
    return (result == 0) ? VECTOR_DB_OK : VECTOR_DB_ERROR_INVALID_PARAM;
}

size_t vector_db_get_index_size(const vector_db_t* db, const char* index_name) {
    if (!db || !index_name) return 0;
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return 0;
    }
    
    pthread_rwlock_rdlock(&entry->lock);
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    
    size_t size = hnswlib_get_current_count(index_name);
    
    pthread_rwlock_unlock(&entry->lock);
    return size;
}

size_t vector_db_get_index_capacity(const vector_db_t* db, const char* index_name) {
    if (!db || !index_name) return 0;
    
    pthread_mutex_lock((pthread_mutex_t*)&db->mutex);
    index_entry_t* entry = find_index(db, index_name);
    if (!entry) {
        pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
        return 0;
    }
    
    size_t capacity = entry->config.max_elements;
    pthread_mutex_unlock((pthread_mutex_t*)&db->mutex);
    
    return capacity;
}

static void* flush_thread_func(void* arg) {
    vector_db_t* db = (vector_db_t*)arg;
    
    while (1) {
        pthread_mutex_lock(&db->flush_mutex);
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        
        // Convert milliseconds to nanoseconds and add to current time
        long long total_ns = ts.tv_nsec + (db->flush_interval_ms * 1000000LL);
        ts.tv_sec += total_ns / 1000000000LL;
        ts.tv_nsec = total_ns % 1000000000LL;
        
        int ret = pthread_cond_timedwait(&db->flush_cond, &db->flush_mutex, &ts);
        
        if (!db->flush_enabled) {
            pthread_mutex_unlock(&db->flush_mutex);
            break;
        }
        
        if (ret == ETIMEDOUT) {
            char* directory = db->flush_directory ? strdup(db->flush_directory) : NULL;
            vector_db_flush_callback_t callback = db->flush_callback;
            void* user_data = db->flush_user_data;
            pthread_mutex_unlock(&db->flush_mutex);
            
            if (directory) {
                vector_db_error_t err = vector_db_save_all(db, directory);
                if (err == VECTOR_DB_OK && callback) {
                    callback(db, user_data);
                }
                free(directory);
            }
        } else {
            pthread_mutex_unlock(&db->flush_mutex);
        }
    }
    
    return NULL;
}

vector_db_error_t vector_db_enable_auto_flush(vector_db_t* db, size_t interval_ms,
                                            const char* directory,
                                            vector_db_flush_callback_t callback,
                                            void* user_data) {
    if (!db || interval_ms == 0 || !directory) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->flush_mutex);
    
    if (db->flush_enabled) {
        pthread_mutex_unlock(&db->flush_mutex);
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    db->flush_interval_ms = interval_ms;
    db->flush_directory = strdup(directory);
    if (!db->flush_directory) {
        pthread_mutex_unlock(&db->flush_mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    db->flush_callback = callback;
    db->flush_user_data = user_data;
    db->flush_enabled = true;
    
    if (pthread_create(&db->flush_thread, NULL, flush_thread_func, db) != 0) {
        free(db->flush_directory);
        db->flush_directory = NULL;
        db->flush_enabled = false;
        pthread_mutex_unlock(&db->flush_mutex);
        return VECTOR_DB_ERROR_MEMORY;
    }
    
    pthread_mutex_unlock(&db->flush_mutex);
    return VECTOR_DB_OK;
}

void vector_db_disable_auto_flush(vector_db_t* db) {
    if (!db) return;
    
    pthread_mutex_lock(&db->flush_mutex);
    if (db->flush_enabled) {
        db->flush_enabled = false;
        pthread_cond_signal(&db->flush_cond);
        pthread_mutex_unlock(&db->flush_mutex);
        
        pthread_join(db->flush_thread, NULL);
        
        free(db->flush_directory);
        db->flush_directory = NULL;
        db->flush_callback = NULL;
        db->flush_user_data = NULL;
    } else {
        pthread_mutex_unlock(&db->flush_mutex);
    }
}

vector_db_error_t vector_db_flush_now(vector_db_t* db) {
    if (!db) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&db->flush_mutex);
    if (!db->flush_enabled || !db->flush_directory) {
        pthread_mutex_unlock(&db->flush_mutex);
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    char* directory = strdup(db->flush_directory);
    vector_db_flush_callback_t callback = db->flush_callback;
    void* user_data = db->flush_user_data;
    pthread_mutex_unlock(&db->flush_mutex);
    
    vector_db_error_t err = vector_db_save_all(db, directory);
    free(directory);
    
    if (err == VECTOR_DB_OK && callback) {
        callback(db, user_data);
    }
    
    return err;
}

const char* vector_db_error_string(vector_db_error_t error) {
    switch (error) {
        case VECTOR_DB_OK: return "Success";
        case VECTOR_DB_ERROR_MEMORY: return "Memory allocation failed";
        case VECTOR_DB_ERROR_INVALID_PARAM: return "Invalid parameter";
        case VECTOR_DB_ERROR_INDEX_NOT_FOUND: return "Index not found";
        case VECTOR_DB_ERROR_ELEMENT_NOT_FOUND: return "Element not found";
        case VECTOR_DB_ERROR_FILE_IO: return "File I/O error";
        case VECTOR_DB_ERROR_SERIALIZATION: return "Serialization error";
        case VECTOR_DB_ERROR_DIMENSION_MISMATCH: return "Vector dimension mismatch";
        case VECTOR_DB_ERROR_INDEX_FULL: return "Index is full";
        default: return "Unknown error";
    }
}

vector_t* vector_create(size_t dimension) {
    if (dimension == 0) return NULL;
    
    vector_t* vec = malloc(sizeof(vector_t));
    if (!vec) return NULL;
    
    vec->data = calloc(dimension, sizeof(float));
    if (!vec->data) {
        free(vec);
        return NULL;
    }
    
    vec->dimension = dimension;
    return vec;
}

void vector_destroy(vector_t* vector) {
    if (!vector) return;
    free(vector->data);
    free(vector);
}

vector_t* vector_clone(const vector_t* vector) {
    if (!vector || !vector->data) return NULL;
    
    vector_t* clone = vector_create(vector->dimension);
    if (!clone) return NULL;
    
    memcpy(clone->data, vector->data, vector->dimension * sizeof(float));
    return clone;
}