#ifndef METADATA_STORE_H
#define METADATA_STORE_H

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

typedef struct {
    size_t chunk_id;
    char* content;
    char* index_name;
    char* type;
    char* source;
    char* importance;
    time_t timestamp;
    char* custom_metadata; // JSON string for additional metadata
} ChunkMetadata;

typedef struct metadata_store metadata_store_t;

// Initialize the metadata store
metadata_store_t* metadata_store_create(const char* base_path);
void metadata_store_destroy(metadata_store_t* store);

// Store metadata for a chunk
int metadata_store_save(metadata_store_t* store, const ChunkMetadata* metadata);

// Retrieve metadata for a chunk
ChunkMetadata* metadata_store_get(metadata_store_t* store, const char* index_name, size_t chunk_id);

// Delete metadata for a chunk
int metadata_store_delete(metadata_store_t* store, const char* index_name, size_t chunk_id);

// List all chunks in an index
ChunkMetadata** metadata_store_list(metadata_store_t* store, const char* index_name, size_t* count);

// Search chunks by metadata fields
ChunkMetadata** metadata_store_search(metadata_store_t* store, const char* index_name, 
                                     const char* query, size_t* count);

// Update metadata for a chunk
int metadata_store_update(metadata_store_t* store, const ChunkMetadata* metadata);

// Free metadata structures
void metadata_store_free_chunk(ChunkMetadata* metadata);
void metadata_store_free_chunks(ChunkMetadata** chunks, size_t count);

// Get singleton instance
metadata_store_t* metadata_store_get_instance(void);

#endif // METADATA_STORE_H