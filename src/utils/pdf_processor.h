#ifndef PDF_PROCESSOR_H
#define PDF_PROCESSOR_H

#include <stddef.h>
#include <time.h>
#include "db/vector_db.h"

typedef struct {
    char *url;
    char *title;
    char *description;
    size_t content_length;
    time_t fetch_time;
} pdf_metadata_t;

typedef struct {
    size_t chunks_processed;
    size_t embeddings_generated;
    size_t vectors_stored;
    char *error;
} pdf_processing_result_t;

/**
 * Download and process a PDF from a URL
 * 
 * @param url The URL to download the PDF from
 * @param vector_db Vector database to store chunks in
 * @param index_name Name of the vector index to use
 * @return Processing result (caller must free)
 */
pdf_processing_result_t* process_pdf_from_url(const char *url, vector_db_t *vector_db, const char *index_name);

/**
 * Process an already downloaded PDF binary data
 * 
 * @param pdf_data Binary PDF data
 * @param data_size Size of PDF data
 * @param metadata Metadata about the PDF
 * @param vector_db Vector database to store chunks in
 * @param index_name Name of the vector index to use
 * @return Processing result (caller must free)
 */
pdf_processing_result_t* process_pdf_data(const unsigned char *pdf_data, size_t data_size,
                                         const pdf_metadata_t *metadata, vector_db_t *vector_db,
                                         const char *index_name);

/**
 * Free PDF processing result
 * 
 * @param result Result to free
 */
void free_pdf_processing_result(pdf_processing_result_t *result);

/**
 * Free PDF metadata
 * 
 * @param metadata Metadata to free
 */
void free_pdf_metadata(pdf_metadata_t *metadata);

#endif // PDF_PROCESSOR_H