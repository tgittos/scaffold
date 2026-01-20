#ifndef PDF_EXTRACTOR_H
#define PDF_EXTRACTOR_H

#include <stddef.h>

// PDF text extraction result structure
typedef struct {
    char *text;           // Extracted text content
    size_t length;        // Length of extracted text
    int page_count;       // Number of pages processed
    char *error;          // Error message if extraction failed (NULL if successful)
} pdf_extraction_result_t;

// PDF extraction configuration
typedef struct {
    int start_page;       // First page to extract (0-based, -1 for all pages)
    int end_page;         // Last page to extract (0-based, -1 for all pages)
    int preserve_layout;  // Whether to preserve layout formatting
    int extract_metadata; // Whether to extract document metadata
} pdf_extraction_config_t;

// Initialize the PDF extraction system
int pdf_extractor_init(void);

// Cleanup the PDF extraction system
void pdf_extractor_cleanup(void);

// Extract text from a PDF file with default configuration.
// Returns NULL on memory allocation failure, or a result struct with error
// field set on other failures. Caller must free result with pdf_free_extraction_result().
pdf_extraction_result_t* pdf_extract_text(const char* pdf_path);

// Extract text from a PDF file with custom configuration.
// Returns NULL on memory allocation failure, or a result struct with error
// field set on other failures. Caller must free result with pdf_free_extraction_result().
pdf_extraction_result_t* pdf_extract_text_with_config(const char* pdf_path, const pdf_extraction_config_t* config);

// Extract text from PDF data in memory.
// Returns NULL on memory allocation failure, or a result struct with error
// field set on other failures. Caller must free result with pdf_free_extraction_result().
pdf_extraction_result_t* pdf_extract_text_from_memory(const unsigned char* pdf_data, size_t data_size);

// Free extraction result
void pdf_free_extraction_result(pdf_extraction_result_t* result);

// Get default extraction configuration
pdf_extraction_config_t pdf_get_default_config(void);

#endif /* PDF_EXTRACTOR_H */