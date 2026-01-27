#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../unity/unity.h"
#include "../../src/pdf/pdf_extractor.h"

void setUp(void) {
    // Set up test environment
}

void tearDown(void) {
    // Clean up after each test
}

void test_pdf_extractor_init_cleanup(void) {
    int result = pdf_extractor_init();
    TEST_ASSERT_EQUAL(0, result);
    
    pdf_extractor_cleanup();
}

void test_pdf_get_default_config(void) {
    pdf_extraction_config_t config = pdf_get_default_config();
    TEST_ASSERT_EQUAL(-1, config.start_page);
    TEST_ASSERT_EQUAL(-1, config.end_page);
}

void test_pdf_extract_text_null_path(void) {
    pdf_extraction_result_t *result = pdf_extract_text(NULL);
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(result->error);
    TEST_ASSERT_NULL(result->text);
    TEST_ASSERT_EQUAL(0, result->length);
    TEST_ASSERT_EQUAL(0, result->page_count);
    
    pdf_free_extraction_result(result);
}

void test_pdf_extract_text_nonexistent_file(void) {
    const char *nonexistent_file = "/tmp/nonexistent_file.pdf";
    
    pdf_extraction_result_t *result = pdf_extract_text(nonexistent_file);
    TEST_ASSERT_NOT_NULL(result);
    
    // With PDFio, we should get an error about file not found
    TEST_ASSERT_NOT_NULL(result->error);
    TEST_ASSERT_NULL(result->text);
    TEST_ASSERT_EQUAL(0, result->page_count);
    
    pdf_free_extraction_result(result);
}

void test_pdf_free_extraction_result_null(void) {
    // Should not crash when called with NULL
    pdf_free_extraction_result(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_pdf_extractor_init_cleanup);
    RUN_TEST(test_pdf_get_default_config);
    RUN_TEST(test_pdf_extract_text_null_path);
    RUN_TEST(test_pdf_extract_text_nonexistent_file);
    RUN_TEST(test_pdf_free_extraction_result_null);
    
    return UNITY_END();
}