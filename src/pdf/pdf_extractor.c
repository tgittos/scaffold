#include "pdf_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

// MuPDF headers - these will be available after we integrate MuPDF
#ifdef HAVE_MUPDF
#include <mupdf/fitz.h>
#endif

static int pdf_extractor_initialized = 0;
#ifdef HAVE_MUPDF
static fz_context *global_ctx = NULL;
#endif

int pdf_extractor_init(void) {
    if (pdf_extractor_initialized) {
        return 0; // Already initialized
    }
    
#ifdef HAVE_MUPDF
    // Initialize MuPDF context
    global_ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
    if (!global_ctx) {
        return -1;
    }
    
    // Register document handlers
    fz_try(global_ctx) {
        fz_register_document_handlers(global_ctx);
    }
    fz_catch(global_ctx) {
        fz_drop_context(global_ctx);
        global_ctx = NULL;
        return -1;
    }
    
    pdf_extractor_initialized = 1;
    return 0;
#else
    // For now, return error if MuPDF is not available
    return -1;
#endif
}

void pdf_extractor_cleanup(void) {
    if (!pdf_extractor_initialized) {
        return;
    }
    
#ifdef HAVE_MUPDF
    if (global_ctx) {
        fz_drop_context(global_ctx);
        global_ctx = NULL;
    }
#endif
    
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

#ifdef HAVE_MUPDF
static char* extract_text_from_stext_page(fz_context *ctx, fz_stext_page *stext_page) {
    fz_buffer *buffer = NULL;
    char *text = NULL;
    
    fz_try(ctx) {
        buffer = fz_new_buffer(ctx, 1024);
        fz_print_stext_page_as_text(ctx, buffer, stext_page);
        
        size_t len = fz_buffer_storage(ctx, buffer, NULL);
        text = malloc(len + 1);
        if (text) {
            memcpy(text, fz_buffer_storage(ctx, buffer, NULL), len);
            text[len] = '\0';
        }
    }
    fz_always(ctx) {
        fz_drop_buffer(ctx, buffer);
    }
    fz_catch(ctx) {
        free(text);
        text = NULL;
    }
    
    return text;
}

static pdf_extraction_result_t* pdf_extract_text_internal(const char* pdf_path, 
                                                         const unsigned char* pdf_data, 
                                                         size_t data_size,
                                                         const pdf_extraction_config_t* config) {
    if (!pdf_extractor_initialized) {
        return create_error_result("PDF extractor not initialized");
    }
    
    if (!config) {
        pdf_extraction_config_t default_config = pdf_get_default_config();
        config = &default_config;
    }
    
    fz_document *doc = NULL;
    fz_page *page = NULL;
    fz_stext_page *stext_page = NULL;
    fz_stext_options stext_options = { 0 };
    pdf_extraction_result_t *result = NULL;
    char *full_text = NULL;
    size_t total_length = 0;
    int page_count = 0;
    
    result = malloc(sizeof(pdf_extraction_result_t));
    if (!result) {
        return create_error_result("Memory allocation failed");
    }
    
    result->text = NULL;
    result->length = 0;
    result->page_count = 0;
    result->error = NULL;
    
    fz_try(global_ctx) {
        // Open document
        if (pdf_path) {
            doc = fz_open_document(global_ctx, pdf_path);
        } else if (pdf_data && data_size > 0) {
            fz_stream *stream = fz_open_memory(global_ctx, pdf_data, data_size);
            doc = fz_open_document_with_stream(global_ctx, "pdf", stream);
            fz_drop_stream(global_ctx, stream);
        } else {
            fz_throw(global_ctx, FZ_ERROR_ARGUMENT, "No PDF source provided");
        }
        
        if (!doc) {
            fz_throw(global_ctx, FZ_ERROR_GENERIC, "Failed to open PDF document");
        }
        
        int total_pages = fz_count_pages(global_ctx, doc);
        int start_page = (config->start_page >= 0) ? config->start_page : 0;
        int end_page = (config->end_page >= 0) ? config->end_page : total_pages - 1;
        
        // Validate page range
        if (start_page >= total_pages) start_page = total_pages - 1;
        if (end_page >= total_pages) end_page = total_pages - 1;
        if (start_page > end_page) start_page = end_page;
        
        // Extract text from each page
        for (int page_num = start_page; page_num <= end_page; page_num++) {
            page = fz_load_page(global_ctx, doc, page_num);
            if (!page) continue;
            
            // Create structured text page
            stext_page = fz_new_stext_page_from_page(global_ctx, page, &stext_options);
            if (stext_page) {
                char *page_text = extract_text_from_stext_page(global_ctx, stext_page);
                if (page_text) {
                    size_t page_text_len = strlen(page_text);
                    if (page_text_len > 0) {
                        // Reallocate full_text to accommodate new page text
                        size_t new_size = total_length + page_text_len + 2; // +2 for newline and null terminator
                        char *new_full_text = realloc(full_text, new_size);
                        if (new_full_text) {
                            full_text = new_full_text;
                            if (total_length > 0) {
                                strcat(full_text + total_length, "\n");
                                total_length++;
                            } else {
                                full_text[0] = '\0';
                            }
                            strcat(full_text + total_length, page_text);
                            total_length += page_text_len;
                            page_count++;
                        }
                    }
                    free(page_text);
                }
                fz_drop_stext_page(global_ctx, stext_page);
                stext_page = NULL;
            }
            
            fz_drop_page(global_ctx, page);
            page = NULL;
        }
        
        // Set results
        result->text = full_text;
        result->length = total_length;
        result->page_count = page_count;
        full_text = NULL; // Transfer ownership to result
    }
    fz_always(global_ctx) {
        if (stext_page) fz_drop_stext_page(global_ctx, stext_page);
        if (page) fz_drop_page(global_ctx, page);
        if (doc) fz_drop_document(global_ctx, doc);
    }
    fz_catch(global_ctx) {
        free(full_text);
        if (result) {
            result->error = strdup(fz_caught_message(global_ctx));
        }
    }
    
    return result;
}
#endif

pdf_extraction_result_t* pdf_extract_text(const char* pdf_path) {
    if (!pdf_path) {
        return create_error_result("PDF path is NULL");
    }
    
#ifdef HAVE_MUPDF
    pdf_extraction_config_t config = pdf_get_default_config();
    return pdf_extract_text_internal(pdf_path, NULL, 0, &config);
#else
    return create_error_result("MuPDF support not compiled");
#endif
}

pdf_extraction_result_t* pdf_extract_text_with_config(const char* pdf_path, const pdf_extraction_config_t* config) {
    if (!pdf_path) {
        return create_error_result("PDF path is NULL");
    }
    
#ifdef HAVE_MUPDF
    return pdf_extract_text_internal(pdf_path, NULL, 0, config);
#else
    (void)config; // Suppress unused parameter warning
    return create_error_result("MuPDF support not compiled");
#endif
}

pdf_extraction_result_t* pdf_extract_text_from_memory(const unsigned char* pdf_data, size_t data_size) {
    if (!pdf_data || data_size == 0) {
        return create_error_result("Invalid PDF data");
    }
    
#ifdef HAVE_MUPDF
    pdf_extraction_config_t config = pdf_get_default_config();
    return pdf_extract_text_internal(NULL, pdf_data, data_size, &config);
#else
    return create_error_result("MuPDF support not compiled");
#endif
}

void pdf_free_extraction_result(pdf_extraction_result_t* result) {
    if (!result) return;
    
    free(result->text);
    free(result->error);
    free(result);
}