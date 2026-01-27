#ifndef PDF_EXTRACTOR_H
#define PDF_EXTRACTOR_H

#include <stddef.h>

typedef struct {
    char *text;
    size_t length;
    int page_count;
    char *error;          // NULL on success; set on extraction failure
} pdf_extraction_result_t;

typedef struct {
    int start_page;       // 0-based, -1 for all pages
    int end_page;         // 0-based, -1 for all pages
} pdf_extraction_config_t;

int pdf_extractor_init(void);
void pdf_extractor_cleanup(void);

// Returns NULL on allocation failure, or a result with error field set on
// other failures. Caller must free with pdf_free_extraction_result().
pdf_extraction_result_t* pdf_extract_text(const char* pdf_path);

// Returns NULL on allocation failure, or a result with error field set on
// other failures. Caller must free with pdf_free_extraction_result().
pdf_extraction_result_t* pdf_extract_text_with_config(const char* pdf_path, const pdf_extraction_config_t* config);

void pdf_free_extraction_result(pdf_extraction_result_t* result);
pdf_extraction_config_t pdf_get_default_config(void);

#endif /* PDF_EXTRACTOR_H */