#include "metadata_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cJSON.h>
#include "../utils/common_utils.h"
#include "../utils/ptrarray.h"

PTRARRAY_DEFINE(ChunkMetadataArray, ChunkMetadata)

struct metadata_store {
    char* base_path;
};

static metadata_store_t* singleton_instance = NULL;

static char* get_metadata_path(metadata_store_t* store, const char* index_name) {
    if (store == NULL || index_name == NULL) return NULL;
    
    size_t path_len = strlen(store->base_path) + strlen(index_name) + 64;
    char* path = malloc(path_len);
    if (path == NULL) return NULL;
    
    snprintf(path, path_len, "%s/%s", store->base_path, index_name);
    
    // Ensure directory exists
    mkdir(store->base_path, 0755);
    mkdir(path, 0755);
    
    return path;
}

static char* get_chunk_filename(const char* index_path, size_t chunk_id) {
    size_t path_len = strlen(index_path) + 64;
    char* filename = malloc(path_len);
    if (filename == NULL) return NULL;
    
    snprintf(filename, path_len, "%s/chunk_%zu.json", index_path, chunk_id);
    return filename;
}

metadata_store_t* metadata_store_create(const char* base_path) {
    metadata_store_t* store = calloc(1, sizeof(metadata_store_t));
    if (store == NULL) return NULL;
    
    if (base_path == NULL) {
        // Use default path
        const char* home = getenv("HOME");
        if (home == NULL) {
            free(store);
            return NULL;
        }
        
        size_t path_len = strlen(home) + 64;
        store->base_path = malloc(path_len);
        if (store->base_path == NULL) {
            free(store);
            return NULL;
        }
        snprintf(store->base_path, path_len, "%s/.local/ralph/metadata", home);
    } else {
        store->base_path = strdup(base_path);
        if (store->base_path == NULL) {
            free(store);
            return NULL;
        }
    }
    
    // Ensure base directory exists
    mkdir(store->base_path, 0755);
    
    return store;
}

void metadata_store_destroy(metadata_store_t* store) {
    if (store == NULL) return;

    if (store == singleton_instance) {
        singleton_instance = NULL;
    }

    free(store->base_path);
    free(store);
}

metadata_store_t* metadata_store_get_instance(void) {
    if (singleton_instance == NULL) {
        singleton_instance = metadata_store_create(NULL);
    }
    return singleton_instance;
}

int metadata_store_save(metadata_store_t* store, const ChunkMetadata* metadata) {
    if (store == NULL || metadata == NULL) return -1;
    
    char* index_path = get_metadata_path(store, metadata->index_name);
    if (index_path == NULL) return -1;
    
    char* filename = get_chunk_filename(index_path, metadata->chunk_id);
    if (filename == NULL) {
        free(index_path);
        return -1;
    }
    
    // Create JSON object
    cJSON* json = cJSON_CreateObject();
    if (json == NULL) {
        free(filename);
        free(index_path);
        return -1;
    }
    
    cJSON_AddNumberToObject(json, "chunk_id", metadata->chunk_id);
    cJSON_AddStringToObject(json, "content", metadata->content ? metadata->content : "");
    cJSON_AddStringToObject(json, "index_name", metadata->index_name ? metadata->index_name : "");
    cJSON_AddStringToObject(json, "type", metadata->type ? metadata->type : "general");
    cJSON_AddStringToObject(json, "source", metadata->source ? metadata->source : "unknown");
    cJSON_AddStringToObject(json, "importance", metadata->importance ? metadata->importance : "normal");
    cJSON_AddNumberToObject(json, "timestamp", metadata->timestamp);
    
    if (metadata->custom_metadata) {
        cJSON* custom = cJSON_Parse(metadata->custom_metadata);
        if (custom) {
            cJSON_AddItemToObject(json, "custom_metadata", custom);
        }
    }
    
    // Write to file
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

ChunkMetadata* metadata_store_get(metadata_store_t* store, const char* index_name, size_t chunk_id) {
    if (store == NULL || index_name == NULL) return NULL;
    
    char* index_path = get_metadata_path(store, index_name);
    if (index_path == NULL) return NULL;
    
    char* filename = get_chunk_filename(index_path, chunk_id);
    if (filename == NULL) {
        free(index_path);
        return NULL;
    }
    
    // Read file
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        free(filename);
        free(index_path);
        return NULL;
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char* json_str = malloc(file_size + 1);
    if (json_str == NULL) {
        fclose(file);
        free(filename);
        free(index_path);
        return NULL;
    }
    
    size_t read_size = fread(json_str, 1, file_size, file);
    json_str[read_size] = '\0';
    fclose(file);
    
    // Parse JSON
    cJSON* json = cJSON_Parse(json_str);
    free(json_str);
    
    if (json == NULL) {
        free(filename);
        free(index_path);
        return NULL;
    }
    
    // Create metadata object
    ChunkMetadata* metadata = calloc(1, sizeof(ChunkMetadata));
    if (metadata == NULL) {
        cJSON_Delete(json);
        free(filename);
        free(index_path);
        return NULL;
    }
    
    // Extract fields
    cJSON* item = cJSON_GetObjectItem(json, "chunk_id");
    if (item) metadata->chunk_id = (size_t)item->valuedouble;

    item = cJSON_GetObjectItem(json, "content");
    if (item && item->valuestring) {
        metadata->content = strdup(item->valuestring);
        if (!metadata->content) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "index_name");
    if (item && item->valuestring) {
        metadata->index_name = strdup(item->valuestring);
        if (!metadata->index_name) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "type");
    if (item && item->valuestring) {
        metadata->type = strdup(item->valuestring);
        if (!metadata->type) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "source");
    if (item && item->valuestring) {
        metadata->source = strdup(item->valuestring);
        if (!metadata->source) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "importance");
    if (item && item->valuestring) {
        metadata->importance = strdup(item->valuestring);
        if (!metadata->importance) goto alloc_error;
    }

    item = cJSON_GetObjectItem(json, "timestamp");
    if (item) metadata->timestamp = (time_t)item->valuedouble;

    item = cJSON_GetObjectItem(json, "custom_metadata");
    if (item) {
        metadata->custom_metadata = cJSON_Print(item);
        if (!metadata->custom_metadata) goto alloc_error;
    }

    cJSON_Delete(json);
    free(filename);
    free(index_path);

    return metadata;

alloc_error:
    metadata_store_free_chunk(metadata);
    cJSON_Delete(json);
    free(filename);
    free(index_path);
    return NULL;
}

int metadata_store_delete(metadata_store_t* store, const char* index_name, size_t chunk_id) {
    if (store == NULL || index_name == NULL) return -1;
    
    char* index_path = get_metadata_path(store, index_name);
    if (index_path == NULL) return -1;
    
    char* filename = get_chunk_filename(index_path, chunk_id);
    if (filename == NULL) {
        free(index_path);
        return -1;
    }
    
    int result = remove(filename);
    
    free(filename);
    free(index_path);
    
    return result;
}

ChunkMetadata** metadata_store_list(metadata_store_t* store, const char* index_name, size_t* count) {
    if (store == NULL || index_name == NULL || count == NULL) return NULL;

    *count = 0;

    char* index_path = get_metadata_path(store, index_name);
    if (index_path == NULL) return NULL;

    DIR* dir = opendir(index_path);
    if (dir == NULL) {
        free(index_path);
        return NULL;
    }

    ChunkMetadataArray arr;
    if (ChunkMetadataArray_init(&arr, NULL) != 0) {
        closedir(dir);
        free(index_path);
        return NULL;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "chunk_") == entry->d_name &&
            strstr(entry->d_name, ".json") != NULL) {

            size_t chunk_id = 0;
            sscanf(entry->d_name, "chunk_%zu.json", &chunk_id);

            ChunkMetadata* metadata = metadata_store_get(store, index_name, chunk_id);
            if (metadata != NULL) {
                if (ChunkMetadataArray_push(&arr, metadata) != 0) {
                    metadata_store_free_chunk(metadata);
                    break;
                }
            }
        }
    }

    closedir(dir);
    free(index_path);

    if (arr.count == 0) {
        ChunkMetadataArray_destroy_shallow(&arr);
        return NULL;
    }

    *count = arr.count;
    ChunkMetadata** result = arr.data;
    arr.data = NULL;
    arr.count = 0;
    arr.capacity = 0;

    return result;
}

ChunkMetadata** metadata_store_search(metadata_store_t* store, const char* index_name,
                                     const char* query, size_t* count) {
    if (store == NULL || index_name == NULL || query == NULL || count == NULL) return NULL;

    size_t total_count = 0;
    ChunkMetadata** all_chunks = metadata_store_list(store, index_name, &total_count);
    if (all_chunks == NULL || total_count == 0) return NULL;

    ChunkMetadataArray matched;
    if (ChunkMetadataArray_init_capacity(&matched, total_count, NULL) != 0) {
        metadata_store_free_chunks(all_chunks, total_count);
        return NULL;
    }

    for (size_t i = 0; i < total_count; i++) {
        ChunkMetadata* chunk = all_chunks[i];
        if (chunk == NULL) continue;

        bool matches = false;

        if (chunk->content && strstr(chunk->content, query) != NULL) {
            matches = true;
        } else if (chunk->type && strstr(chunk->type, query) != NULL) {
            matches = true;
        } else if (chunk->source && strstr(chunk->source, query) != NULL) {
            matches = true;
        } else if (chunk->custom_metadata && strstr(chunk->custom_metadata, query) != NULL) {
            matches = true;
        }

        if (matches) {
            ChunkMetadataArray_push(&matched, chunk);
            all_chunks[i] = NULL;
        }
    }

    for (size_t i = 0; i < total_count; i++) {
        if (all_chunks[i] != NULL) {
            metadata_store_free_chunk(all_chunks[i]);
        }
    }
    free(all_chunks);

    if (matched.count == 0) {
        ChunkMetadataArray_destroy_shallow(&matched);
        *count = 0;
        return NULL;
    }

    *count = matched.count;
    ChunkMetadata** result = matched.data;
    matched.data = NULL;
    matched.count = 0;
    matched.capacity = 0;

    return result;
}

int metadata_store_update(metadata_store_t* store, const ChunkMetadata* metadata) {
    // Update is the same as save (overwrites existing file)
    return metadata_store_save(store, metadata);
}

void metadata_store_free_chunk(ChunkMetadata* metadata) {
    if (metadata == NULL) return;
    
    free(metadata->content);
    free(metadata->index_name);
    free(metadata->type);
    free(metadata->source);
    free(metadata->importance);
    free(metadata->custom_metadata);
    free(metadata);
}

void metadata_store_free_chunks(ChunkMetadata** chunks, size_t count) {
    if (chunks == NULL) return;
    
    for (size_t i = 0; i < count; i++) {
        metadata_store_free_chunk(chunks[i]);
    }
    free(chunks);
}