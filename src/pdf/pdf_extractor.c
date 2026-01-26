#include "pdf_extractor.h"
#include "utils/darray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

// PDFio headers
#include <pdfio.h>

// Define a dynamic character buffer for growing text extraction
DARRAY_IMPL(CharBuffer, char)

// Buffer size constants
#define PDF_INITIAL_BUFFER_SIZE 1024
#define PDF_TOKEN_BUFFER_SIZE 1024

// Note: This module is NOT thread-safe. The initialization state is tracked
// with a global variable without synchronization. Callers must ensure that
// pdf_extractor_init() and pdf_extractor_cleanup() are not called concurrently.
static int pdf_extractor_initialized = 0;

int pdf_extractor_init(void) {
    if (pdf_extractor_initialized) {
        return 0; // Already initialized
    }
    
    // PDFio doesn't require global initialization
    pdf_extractor_initialized = 1;
    return 0;
}

void pdf_extractor_cleanup(void) {
    if (!pdf_extractor_initialized) {
        return;
    }
    
    // PDFio doesn't require global cleanup
    
    pdf_extractor_initialized = 0;
}

pdf_extraction_config_t pdf_get_default_config(void) {
    pdf_extraction_config_t config = {
        .start_page = -1,        // All pages
        .end_page = -1,          // All pages
        .preserve_layout = 1,    // Preserve layout by default
        .extract_metadata = 0    // Don't extract metadata by default
    };
    return config;
}

static pdf_extraction_result_t* create_error_result(const char* error_msg) {
    pdf_extraction_result_t* result = malloc(sizeof(pdf_extraction_result_t));
    if (!result) return NULL;
    
    result->text = NULL;
    result->length = 0;
    result->page_count = 0;
    result->error = strdup(error_msg);
    
    return result;
}

static char* extract_text_from_page(pdfio_file_t *pdf, size_t page_num) {
    pdfio_obj_t *page = pdfioFileGetPage(pdf, page_num);
    if (!page) {
        return NULL;
    }

    size_t num_streams = pdfioPageGetNumStreams(page);
    CharBuffer text_buffer;
    if (CharBuffer_init_capacity(&text_buffer, PDF_INITIAL_BUFFER_SIZE) != 0) {
        return NULL;
    }

    // Process all streams on the page
    for (size_t j = 0; j < num_streams; j++) {
        pdfio_stream_t *st = pdfioPageOpenStream(page, j, true);
        if (!st) {
            continue;
        }

        // Extract text tokens from the stream, based on pdfiototext.c
        char buffer[PDF_TOKEN_BUFFER_SIZE];
        bool first = true;
        while (pdfioStreamGetToken(st, buffer, sizeof(buffer))) {
            if (buffer[0] == '(') {
                // Text string - extract content after the opening parenthesis
                size_t token_len = strlen(buffer + 1);
                if (token_len > 0) {
                    // Add space before token if not first
                    if (!first) {
                        if (CharBuffer_push(&text_buffer, ' ') != 0) {
                            CharBuffer_destroy(&text_buffer);
                            pdfioStreamClose(st);
                            return NULL;
                        }
                    } else {
                        first = false;
                    }

                    // Reserve space for the token
                    if (CharBuffer_reserve(&text_buffer, text_buffer.count + token_len) != 0) {
                        CharBuffer_destroy(&text_buffer);
                        pdfioStreamClose(st);
                        return NULL;
                    }

                    // Copy the token
                    memcpy(text_buffer.data + text_buffer.count, buffer + 1, token_len);
                    text_buffer.count += token_len;
                }
            } else if (!strcmp(buffer, "Td") || !strcmp(buffer, "TD") || !strcmp(buffer, "T*") ||
                       !strcmp(buffer, "'") || !strcmp(buffer, "\"")) {
                // Text positioning commands - add newline
                if (!first) {
                    if (CharBuffer_push(&text_buffer, '\n') != 0) {
                        CharBuffer_destroy(&text_buffer);
                        pdfioStreamClose(st);
                        return NULL;
                    }
                    first = true;
                }
            }
        }

        if (!first && text_buffer.count > 0 && text_buffer.data[text_buffer.count - 1] != '\n') {
            if (CharBuffer_push(&text_buffer, '\n') != 0) {
                CharBuffer_destroy(&text_buffer);
                pdfioStreamClose(st);
                return NULL;
            }
        }

        pdfioStreamClose(st);
    }

    // Add null terminator
    if (CharBuffer_push(&text_buffer, '\0') != 0) {
        CharBuffer_destroy(&text_buffer);
        return NULL;
    }

    // Return the buffer, transferring ownership to caller
    char* result = text_buffer.data;
    text_buffer.data = NULL;  // Don't free the data
    CharBuffer_destroy(&text_buffer);

    return result;
}

static pdf_extraction_result_t* pdf_extract_text_internal(const char* pdf_path,
                                                         const unsigned char* pdf_data,
                                                         size_t data_size,
                                                         const pdf_extraction_config_t* config) {
    if (!pdf_extractor_initialized) {
        return create_error_result("PDF extractor not initialized");
    }

    // Declare default_config at function scope to avoid dangling pointer
    // when config is NULL and we assign config = &default_config
    pdf_extraction_config_t default_config;
    if (!config) {
        default_config = pdf_get_default_config();
        config = &default_config;
    }

    pdfio_file_t *pdf = NULL;
    pdf_extraction_result_t *result = NULL;
    char *full_text = NULL;
    size_t total_length = 0;
    int page_count = 0;

    result = malloc(sizeof(pdf_extraction_result_t));
    if (!result) {
        return NULL;  // Cannot allocate even for error result
    }

    result->text = NULL;
    result->length = 0;
    result->page_count = 0;
    result->error = NULL;

    // Open document
    if (pdf_path) {
        pdf = pdfioFileOpen(pdf_path, NULL, NULL, NULL, NULL);
    } else if (pdf_data && data_size > 0) {
        // PDFio doesn't support direct memory input - return error for now
        result->error = strdup("Memory-based PDF reading not supported with PDFio");
        return result;
    } else {
        result->error = strdup("No PDF source provided");
        return result;
    }

    if (!pdf) {
        result->error = strdup("Failed to open PDF document");
        return result;
    }

    size_t total_pages = pdfioFileGetNumPages(pdf);
    size_t start_page = (config->start_page >= 0) ? (size_t)config->start_page : 0;
    size_t end_page = (config->end_page >= 0) ? (size_t)config->end_page : total_pages - 1;

    // Validate page range
    if (start_page >= total_pages) start_page = total_pages - 1;
    if (end_page >= total_pages) end_page = total_pages - 1;
    if (start_page > end_page) start_page = end_page;

    // Extract text from each page
    for (size_t page_num = start_page; page_num <= end_page; page_num++) {
        char *page_text = extract_text_from_page(pdf, page_num);
        if (page_text) {
            size_t page_text_len = strlen(page_text);
            if (page_text_len > 0) {
                // Check for size_t overflow before calculation
                if (page_text_len > SIZE_MAX - total_length - 2) {
                    free(page_text);
                    free(full_text);
                    pdfioFileClose(pdf);
                    result->error = strdup("PDF too large: size overflow");
                    return result;
                }

                // Reallocate full_text to accommodate new page text
                // +2 for newline separator and null terminator
                size_t new_size = total_length + page_text_len + 2;
                char *new_full_text = realloc(full_text, new_size);
                if (!new_full_text) {
                    // Allocation failed - clean up and return error
                    free(page_text);
                    free(full_text);
                    pdfioFileClose(pdf);
                    result->error = strdup("Memory allocation failed during text extraction");
                    return result;
                }
                full_text = new_full_text;

                // Append newline separator if not the first page
                if (total_length > 0) {
                    full_text[total_length] = '\n';
                    total_length++;
                }

                // Copy page text using memcpy for explicit control
                memcpy(full_text + total_length, page_text, page_text_len);
                total_length += page_text_len;
                full_text[total_length] = '\0';  // Null-terminate

                page_count++;
            }
            free(page_text);
        }
    }

    pdfioFileClose(pdf);

    // Set results
    result->text = full_text;
    result->length = total_length;
    result->page_count = page_count;

    return result;
}

pdf_extraction_result_t* pdf_extract_text(const char* pdf_path) {
    if (!pdf_path) {
        return create_error_result("PDF path is NULL");
    }
    
    pdf_extraction_config_t config = pdf_get_default_config();
    return pdf_extract_text_internal(pdf_path, NULL, 0, &config);
}

pdf_extraction_result_t* pdf_extract_text_with_config(const char* pdf_path, const pdf_extraction_config_t* config) {
    if (!pdf_path) {
        return create_error_result("PDF path is NULL");
    }
    
    return pdf_extract_text_internal(pdf_path, NULL, 0, config);
}

pdf_extraction_result_t* pdf_extract_text_from_memory(const unsigned char* pdf_data, size_t data_size) {
    if (!pdf_data || data_size == 0) {
        return create_error_result("Invalid PDF data");
    }
    
    pdf_extraction_config_t config = pdf_get_default_config();
    return pdf_extract_text_internal(NULL, pdf_data, data_size, &config);
}

void pdf_free_extraction_result(pdf_extraction_result_t* result) {
    if (!result) return;
    
    free(result->text);
    free(result->error);
    free(result);
}