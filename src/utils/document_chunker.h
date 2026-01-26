#ifndef DOCUMENT_CHUNKER_H
#define DOCUMENT_CHUNKER_H

#include <stddef.h>
#include "darray.h"

typedef struct {
    char *text;           // Chunk text content
    size_t length;        // Length of chunk text
    size_t start_offset;  // Starting position in original document
    size_t end_offset;    // Ending position in original document
    int chunk_index;      // Index of this chunk in the document
} document_chunk_t;

DARRAY_DECLARE(ChunkArray, document_chunk_t)

typedef struct {
    ChunkArray chunks;         // Array of chunks
    char *error;               // Error message if chunking failed
} chunking_result_t;

typedef struct {
    size_t max_chunk_size;     // Maximum size of each chunk in characters
    size_t overlap_size;       // Number of characters to overlap between chunks
    int preserve_sentences;    // Whether to avoid breaking sentences (1 = yes, 0 = no)
    int preserve_paragraphs;   // Whether to avoid breaking paragraphs (1 = yes, 0 = no)
    size_t min_chunk_size;     // Minimum size for a chunk to be created
} chunking_config_t;

/**
 * Get default chunking configuration optimized for embeddings
 */
chunking_config_t chunker_get_default_config(void);

/**
 * Get chunking configuration optimized for PDF documents
 */
chunking_config_t chunker_get_pdf_config(void);

/**
 * Chunk a text document into smaller pieces
 * 
 * @param text The text to chunk
 * @param config Chunking configuration (NULL for default)
 * @return Chunking result with chunks or error message
 */
chunking_result_t* chunk_document(const char *text, const chunking_config_t *config);

/**
 * Free chunking result and all associated memory
 * 
 * @param result The chunking result to free
 */
void free_chunking_result(chunking_result_t *result);

/**
 * Calculate optimal chunk size for a given embedding dimension
 * This helps balance context preservation with embedding quality
 * 
 * @param embedding_dimension The dimension of the embedding model
 * @return Recommended chunk size in characters
 */
size_t calculate_optimal_chunk_size(size_t embedding_dimension);

#endif // DOCUMENT_CHUNKER_H