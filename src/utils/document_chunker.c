#include "document_chunker.h"
#include "common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

DARRAY_DEFINE(ChunkArray, document_chunk_t)

static chunking_result_t* create_error_result(const char *error_msg) {
    chunking_result_t *result = malloc(sizeof(chunking_result_t));
    if (!result) return NULL;

    ChunkArray_init(&result->chunks);
    result->error = safe_strdup(error_msg);

    return result;
}

chunking_config_t chunker_get_default_config(void) {
    chunking_config_t config = {
        .max_chunk_size = 1000,        // 1000 characters for good embedding quality
        .overlap_size = 200,           // 20% overlap for context preservation
        .preserve_sentences = 1,       // Try to preserve sentence boundaries
        .preserve_paragraphs = 0,      // Don't strictly preserve paragraphs
        .min_chunk_size = 100         // Minimum 100 characters per chunk
    };
    return config;
}

chunking_config_t chunker_get_pdf_config(void) {
    chunking_config_t config = {
        .max_chunk_size = 1500,        // Larger chunks for PDF content
        .overlap_size = 300,           // More overlap for PDFs
        .preserve_sentences = 1,       // Preserve sentences
        .preserve_paragraphs = 1,      // Try to preserve paragraphs in PDFs
        .min_chunk_size = 150         // Larger minimum for PDFs
    };
    return config;
}

size_t calculate_optimal_chunk_size(size_t embedding_dimension) {
    // Rule of thumb: higher dimensional embeddings can handle larger chunks
    // but there are diminishing returns after a certain point
    if (embedding_dimension >= 1536) {
        return 1500;  // Large models like text-embedding-3-large
    } else if (embedding_dimension >= 768) {
        return 1000;  // Medium models like text-embedding-3-small
    } else if (embedding_dimension >= 384) {
        return 750;   // Smaller models
    } else {
        return 500;   // Very small models
    }
}

static int is_sentence_boundary(const char *text, size_t pos, size_t text_len) {
    if (pos >= text_len) return 0;
    
    char ch = text[pos];
    if (ch != '.' && ch != '!' && ch != '?') return 0;
    
    // Look ahead for whitespace or end of text
    size_t next_pos = pos + 1;
    if (next_pos >= text_len) return 1;  // End of text
    
    return isspace(text[next_pos]);
}

static int is_paragraph_boundary(const char *text, size_t pos, size_t text_len) {
    if (pos >= text_len) return 0;
    
    // Look for double newlines or similar paragraph markers
    if (text[pos] == '\n') {
        size_t next_pos = pos + 1;
        if (next_pos < text_len && text[next_pos] == '\n') {
            return 1;  // Found double newline
        }
        // Check for newline followed by whitespace and another newline
        while (next_pos < text_len && isspace(text[next_pos]) && text[next_pos] != '\n') {
            next_pos++;
        }
        if (next_pos < text_len && text[next_pos] == '\n') {
            return 1;
        }
    }
    
    return 0;
}

static size_t find_best_split_point(const char *text, size_t start, size_t max_end, 
                                   const chunking_config_t *config) {
    size_t text_len = strlen(text);
    size_t search_end = (max_end < text_len) ? max_end : text_len;
    
    // If we're at or near the end, just return the end
    if (search_end >= text_len || search_end - start <= config->min_chunk_size) {
        return text_len;
    }
    
    // Search backwards from max_end to find a good split point
    for (size_t pos = search_end; pos > start + config->min_chunk_size; pos--) {
        // First priority: paragraph boundaries (if enabled)
        if (config->preserve_paragraphs && is_paragraph_boundary(text, pos - 1, text_len)) {
            return pos;
        }
        
        // Second priority: sentence boundaries (if enabled)
        if (config->preserve_sentences && is_sentence_boundary(text, pos - 1, text_len)) {
            // Make sure we include the punctuation
            return pos;
        }
    }
    
    // Third priority: word boundaries (whitespace)
    for (size_t pos = search_end; pos > start + config->min_chunk_size; pos--) {
        if (isspace(text[pos - 1])) {
            return pos;
        }
    }
    
    // Last resort: hard split at max_end
    return search_end;
}

static void trim_whitespace(char *text, size_t *length) {
    if (!text || *length == 0) return;
    
    // Trim leading whitespace
    size_t start = 0;
    while (start < *length && isspace(text[start])) {
        start++;
    }
    
    // Trim trailing whitespace
    size_t end = *length;
    while (end > start && isspace(text[end - 1])) {
        end--;
    }
    
    // Move content to start of buffer if needed
    if (start > 0) {
        memmove(text, text + start, end - start);
    }
    
    *length = end - start;
    text[*length] = '\0';
}

chunking_result_t* chunk_document(const char *text, const chunking_config_t *config) {
    if (!text) {
        return create_error_result("Input text is NULL");
    }
    
    size_t text_len = strlen(text);
    if (text_len == 0) {
        return create_error_result("Input text is empty");
    }
    
    // Use default config if none provided
    chunking_config_t default_config;
    if (!config) {
        default_config = chunker_get_default_config();
        config = &default_config;
    }
    
    // Validate configuration
    if (config->max_chunk_size < config->min_chunk_size) {
        return create_error_result("max_chunk_size must be >= min_chunk_size");
    }
    
    if (config->overlap_size >= config->max_chunk_size) {
        return create_error_result("overlap_size must be < max_chunk_size");
    }
    
    // If the text is smaller than max_chunk_size, return it as a single chunk
    if (text_len <= config->max_chunk_size) {
        chunking_result_t *result = malloc(sizeof(chunking_result_t));
        if (!result) {
            return create_error_result("Memory allocation failed");
        }

        ChunkArray_init_capacity(&result->chunks, 1);
        result->error = NULL;

        document_chunk_t chunk;
        chunk.text = safe_strdup(text);
        chunk.length = text_len;
        chunk.start_offset = 0;
        chunk.end_offset = text_len;
        chunk.chunk_index = 0;

        if (!chunk.text) {
            ChunkArray_destroy(&result->chunks);
            free(result);
            return create_error_result("Memory allocation failed");
        }

        // Apply whitespace trimming to single chunk as well
        trim_whitespace(chunk.text, &chunk.length);

        if (ChunkArray_push(&result->chunks, chunk) != 0) {
            free(chunk.text);
            ChunkArray_destroy(&result->chunks);
            free(result);
            return create_error_result("Memory allocation failed");
        }

        return result;
    }
    
    // Estimate number of chunks needed
    size_t estimated_chunks = (text_len / (config->max_chunk_size - config->overlap_size)) + 2;

    chunking_result_t *result = malloc(sizeof(chunking_result_t));
    if (!result) {
        return create_error_result("Memory allocation failed");
    }

    ChunkArray_init_capacity(&result->chunks, estimated_chunks);
    result->error = NULL;
    
    size_t current_pos = 0;
    int chunk_index = 0;
    
    while (current_pos < text_len) {
        // Find the end position for this chunk
        size_t chunk_end = find_best_split_point(text, current_pos, 
                                                current_pos + config->max_chunk_size, config);
        
        // Make sure we don't create chunks that are too small (except for the last chunk)
        if (chunk_end - current_pos < config->min_chunk_size && chunk_end < text_len) {
            // Extend the chunk to meet minimum size, but don't exceed max size too much
            size_t extended_end = current_pos + config->min_chunk_size;
            if (extended_end <= current_pos + config->max_chunk_size * 1.2) {
                chunk_end = find_best_split_point(text, current_pos, extended_end, config);
            }
        }
        
        // Create the chunk
        size_t chunk_length = chunk_end - current_pos;

        document_chunk_t chunk;

        // Allocate and copy chunk text
        chunk.text = malloc(chunk_length + 1);
        if (!chunk.text) {
            // Cleanup and return error
            for (size_t i = 0; i < result->chunks.count; i++) {
                free(result->chunks.data[i].text);
            }
            ChunkArray_destroy(&result->chunks);
            free(result);
            return create_error_result("Memory allocation failed for chunk text");
        }

        memcpy(chunk.text, text + current_pos, chunk_length);
        chunk.text[chunk_length] = '\0';

        // Trim whitespace from chunk
        trim_whitespace(chunk.text, &chunk_length);

        // Set chunk metadata
        chunk.length = chunk_length;
        chunk.start_offset = current_pos;
        chunk.end_offset = chunk_end;
        chunk.chunk_index = chunk_index;

        if (ChunkArray_push(&result->chunks, chunk) != 0) {
            // Cleanup and return error
            free(chunk.text);
            for (size_t i = 0; i < result->chunks.count; i++) {
                free(result->chunks.data[i].text);
            }
            ChunkArray_destroy(&result->chunks);
            free(result);
            return create_error_result("Memory allocation failed during chunking");
        }

        chunk_index++;
        
        // Move to next chunk position with overlap
        if (chunk_end >= text_len) {
            break;  // We've reached the end
        }
        
        // Calculate next position with overlap
        size_t next_pos = chunk_end;
        if (config->overlap_size > 0 && chunk_end > config->overlap_size) {
            next_pos = chunk_end - config->overlap_size;
        }
        
        // Make sure we're making progress
        if (next_pos <= current_pos) {
            next_pos = current_pos + 1;
        }
        
        current_pos = next_pos;
    }
    
    return result;
}

void free_chunking_result(chunking_result_t *result) {
    if (!result) return;

    for (size_t i = 0; i < result->chunks.count; i++) {
        free(result->chunks.data[i].text);
    }
    ChunkArray_destroy(&result->chunks);

    free(result->error);
    free(result);
}
