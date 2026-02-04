#include "pdf_processor.h"
#include "network/http_client.h"
#include "pdf/pdf_extractor.h"
#include "util/document_chunker.h"
#include "llm/embeddings.h"
#include "config.h"
#include "util/common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>

static pdf_processing_result_t* create_error_result(const char *error_msg) {
    pdf_processing_result_t *result = malloc(sizeof(pdf_processing_result_t));
    if (!result) return NULL;
    
    result->chunks_processed = 0;
    result->embeddings_generated = 0;
    result->vectors_stored = 0;
    result->error = safe_strdup(error_msg);
    
    return result;
}

static char* generate_temp_filename(const char *url) {
    (void)url;
    char *temp_path = malloc(256);
    if (!temp_path) return NULL;

    snprintf(temp_path, 256, "/tmp/ralph_pdf_%d_%ld.pdf", getpid(), time(NULL));
    
    return temp_path;
}

static int save_data_to_temp_file(const unsigned char *data, size_t size, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        return -1;
    }
    
    size_t written = fwrite(data, 1, size, fp);
    fclose(fp);
    
    return (written == size) ? 0 : -1;
}

static embeddings_config_t* get_embeddings_config(void) {
    static embeddings_config_t config = {0};
    static int initialized = 0;
    
    if (!initialized) {
        ralph_config_t *global_config = config_get();
        const char *api_key, *model, *api_url;
        
        if (global_config) {
            api_key = global_config->openai_api_key;
            model = global_config->embedding_model;
            api_url = global_config->openai_api_url;
        } else {
            api_key = getenv("OPENAI_API_KEY");
            model = getenv("EMBEDDING_MODEL");
            api_url = getenv("OPENAI_API_URL");
        }
        
        if (!model) model = "text-embedding-3-small";
        
        if (embeddings_init(&config, model, api_key, api_url) == 0) {
            initialized = 1;
        }
    }
    
    return initialized ? &config : NULL;
}

pdf_processing_result_t* process_pdf_data(const unsigned char *pdf_data, size_t data_size, 
                                         const pdf_metadata_t *metadata, vector_db_t *vector_db, 
                                         const char *index_name) {
    if (!pdf_data || data_size == 0 || !vector_db || !index_name) {
        return create_error_result("Invalid parameters");
    }
    
    pdf_processing_result_t *result = malloc(sizeof(pdf_processing_result_t));
    if (!result) {
        return create_error_result("Memory allocation failed");
    }
    
    result->chunks_processed = 0;
    result->embeddings_generated = 0;
    result->vectors_stored = 0;
    result->error = NULL;
    
    // PDFio requires a file path, so we must write to a temp file first
    char *temp_path = generate_temp_filename(metadata ? metadata->url : "unknown");
    if (!temp_path) {
        free(result);
        return create_error_result("Failed to generate temp filename");
    }
    
    if (save_data_to_temp_file(pdf_data, data_size, temp_path) != 0) {
        free(temp_path);
        free(result);
        return create_error_result("Failed to save PDF to temp file");
    }
    
    if (pdf_extractor_init() != 0) {
        unlink(temp_path);
        free(temp_path);
        free(result);
        return create_error_result("Failed to initialize PDF extractor");
    }
    
    pdf_extraction_config_t pdf_config = pdf_get_default_config();
    pdf_extraction_result_t *extraction_result = pdf_extract_text_with_config(temp_path, &pdf_config);
    
    unlink(temp_path);
    free(temp_path);
    
    if (!extraction_result) {
        free(result);
        return create_error_result("PDF extraction failed");
    }
    
    if (extraction_result->error) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "PDF extraction error: %s", extraction_result->error);
        pdf_free_extraction_result(extraction_result);
        free(result);
        return create_error_result(error_msg);
    }
    
    if (!extraction_result->text || extraction_result->length == 0) {
        pdf_free_extraction_result(extraction_result);
        free(result);
        return create_error_result("No text extracted from PDF");
    }
    
    chunking_config_t chunk_config = chunker_get_pdf_config();
    chunking_result_t *chunks = chunk_document(extraction_result->text, &chunk_config);
    
    if (!chunks) {
        pdf_free_extraction_result(extraction_result);
        free(result);
        return create_error_result("Document chunking failed");
    }
    
    if (chunks->error) {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg), "Chunking error: %s", chunks->error);
        free_chunking_result(chunks);
        pdf_free_extraction_result(extraction_result);
        free(result);
        return create_error_result(error_msg);
    }
    
    result->chunks_processed = chunks->chunks.count;
    
    embeddings_config_t *embed_config = get_embeddings_config();
    if (!embed_config) {
        free_chunking_result(chunks);
        pdf_free_extraction_result(extraction_result);
        free(result);
        return create_error_result("Embeddings not configured (missing API key?)");
    }
    
    for (size_t i = 0; i < chunks->chunks.count; i++) {
        document_chunk_t *chunk = &chunks->chunks.data[i];
        
        embedding_vector_t embedding = {0};
        if (embeddings_get_vector(embed_config, chunk->text, &embedding) != 0) {
            continue;
        }
        
        result->embeddings_generated++;
        
        vector_t vector = {
            .data = embedding.data,
            .dimension = embedding.dimension
        };
        
        // Hash URL + chunk index to produce a stable, unique label per chunk
        size_t label = 0;
        if (metadata && metadata->url) {
            const char *url = metadata->url;
            for (const char *p = url; *p; p++) {
                label = label * 31 + *p;
            }
            label = label * 1000 + i;
        } else {
            label = time(NULL) * 1000 + i;
        }
        
        vector_db_error_t db_error = vector_db_add_vector(vector_db, index_name, &vector, label);
        if (db_error == VECTOR_DB_OK) {
            result->vectors_stored++;
        }
        
        // Ownership of embedding.data transferred to the vector database
        embedding.data = NULL;
        embeddings_free_vector(&embedding);
    }
    
    free_chunking_result(chunks);
    pdf_free_extraction_result(extraction_result);
    
    if (result->vectors_stored == 0 && result->chunks_processed > 0) {
        result->error = safe_strdup("No vectors were stored successfully");
    }
    
    return result;
}

pdf_processing_result_t* process_pdf_from_url(const char *url, vector_db_t *vector_db, const char *index_name) {
    if (!url || !vector_db || !index_name) {
        return create_error_result("Invalid parameters");
    }
    
    struct HTTPResponse response = {0};
    if (http_get(url, &response) != 0) {
        return create_error_result("Failed to download PDF from URL");
    }
    
    if (!response.data || response.size == 0) {
        cleanup_response(&response);
        return create_error_result("Empty response from URL");
    }
    
    pdf_metadata_t metadata = {
        .url = safe_strdup(url),
        .title = NULL,
        .description = NULL,
        .content_length = response.size,
        .fetch_time = time(NULL)
    };
    
    pdf_processing_result_t *result = process_pdf_data((unsigned char*)response.data,
                                                      response.size, &metadata, 
                                                      vector_db, index_name);
    
    cleanup_response(&response);
    free_pdf_metadata(&metadata);
    
    return result;
}

void free_pdf_processing_result(pdf_processing_result_t *result) {
    if (!result) return;
    
    free(result->error);
    free(result);
}

void free_pdf_metadata(pdf_metadata_t *metadata) {
    if (!metadata) return;
    
    free(metadata->url);
    free(metadata->title);
    free(metadata->description);
    
    memset(metadata, 0, sizeof(pdf_metadata_t));
}