#ifndef METADATA_STORE_H
#define METADATA_STORE_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include "../utils/ptrarray.h"

typedef struct {
    size_t chunk_id;
    char* content;
    char* index_name;
    char* type;
    char* source;
    char* importance;
    time_t timestamp;
    char* custom_metadata;
} ChunkMetadata;

PTRARRAY_DECLARE(ChunkMetadataArray, ChunkMetadata)

typedef struct metadata_store metadata_store_t;

metadata_store_t* metadata_store_create(const char* base_path);
void metadata_store_destroy(metadata_store_t* store);

int metadata_store_save(metadata_store_t* store, const ChunkMetadata* metadata);
ChunkMetadata* metadata_store_get(metadata_store_t* store, const char* index_name, size_t chunk_id);
int metadata_store_delete(metadata_store_t* store, const char* index_name, size_t chunk_id);
ChunkMetadata** metadata_store_list(metadata_store_t* store, const char* index_name, size_t* count);
ChunkMetadata** metadata_store_search(metadata_store_t* store, const char* index_name,
                                     const char* query, size_t* count);

// Overwrites existing metadata for the chunk, equivalent to save.
int metadata_store_update(metadata_store_t* store, const ChunkMetadata* metadata);

void metadata_store_free_chunk(ChunkMetadata* metadata);
void metadata_store_free_chunks(ChunkMetadata** chunks, size_t count);

metadata_store_t* metadata_store_get_instance(void);

#endif // METADATA_STORE_H