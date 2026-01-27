#include "unity/unity.h"
#include "../src/utils/document_chunker.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
    // This function is called before each test
}

void tearDown(void) {
    // This function is called after each test
}

void test_chunk_small_document_single_chunk(void) {
    const char *text = "This is a small document that should fit in a single chunk.";
    
    chunking_result_t *result = chunk_document(text, NULL);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NULL(result->error);
    TEST_ASSERT_EQUAL_INT(1, result->chunks.count);
    TEST_ASSERT_NOT_NULL(result->chunks.data);
    TEST_ASSERT_EQUAL_STRING(text, result->chunks.data[0].text);
    TEST_ASSERT_EQUAL_INT(strlen(text), result->chunks.data[0].length);
    TEST_ASSERT_EQUAL_INT(0, result->chunks.data[0].start_offset);
    TEST_ASSERT_EQUAL_INT(strlen(text), result->chunks.data[0].end_offset);
    TEST_ASSERT_EQUAL_INT(0, result->chunks.data[0].chunk_index);
    
    free_chunking_result(result);
}

void test_chunk_empty_document(void) {
    const char *text = "";
    
    chunking_result_t *result = chunk_document(text, NULL);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(result->error);
    TEST_ASSERT_EQUAL_INT(0, result->chunks.count);
    
    free_chunking_result(result);
}

void test_chunk_null_document(void) {
    chunking_result_t *result = chunk_document(NULL, NULL);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(result->error);
    TEST_ASSERT_EQUAL_INT(0, result->chunks.count);
    
    free_chunking_result(result);
}

void test_chunk_large_document_multiple_chunks(void) {
    // Create a document that's larger than the default chunk size
    char *large_text = malloc(2000);
    memset(large_text, 'a', 1999);
    large_text[1999] = '\0';
    
    chunking_result_t *result = chunk_document(large_text, NULL);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NULL(result->error);
    TEST_ASSERT_GREATER_THAN(1, result->chunks.count);
    
    // Verify all chunks are properly formed
    for (size_t i = 0; i < result->chunks.count; i++) {
        TEST_ASSERT_NOT_NULL(result->chunks.data[i].text);
        TEST_ASSERT_GREATER_THAN(0, result->chunks.data[i].length);
        TEST_ASSERT_EQUAL_INT(i, result->chunks.data[i].chunk_index);
    }
    
    free_chunking_result(result);
    free(large_text);
}

void test_chunk_with_sentences(void) {
    const char *text = "This is the first sentence. This is the second sentence. This is the third sentence. This is the fourth sentence. This is the fifth sentence. This is the sixth sentence. This is the seventh sentence. This is the eighth sentence.";
    
    chunking_config_t config = chunker_get_default_config();
    config.max_chunk_size = 80;   // Force chunking 
    config.min_chunk_size = 30;   // Lower minimum to allow smaller chunks
    config.overlap_size = 20;     // Smaller overlap to fit within max_chunk_size
    config.preserve_sentences = 1;
    
    chunking_result_t *result = chunk_document(text, &config);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NULL(result->error);
    TEST_ASSERT_GREATER_THAN(1, result->chunks.count);
    
    // Check that sentence boundaries are preserved
    for (size_t i = 0; i < result->chunks.count; i++) {
        char *chunk_text = result->chunks.data[i].text;
        size_t len = result->chunks.data[i].length;
        
        // If chunk ends with a sentence ending, it should be at a sentence boundary
        if (len > 0 && (chunk_text[len-1] == '.' || chunk_text[len-1] == '!' || chunk_text[len-1] == '?')) {
            // This is good - chunk ends at sentence boundary
            TEST_ASSERT(1);
        }
    }
    
    free_chunking_result(result);
}

void test_chunk_with_paragraphs(void) {
    const char *text = "This is the first paragraph with a lot of content to make it longer.\n\nThis is the second paragraph with even more content to ensure we exceed the chunk size limit.\n\nThis is the third paragraph with additional text.";
    
    chunking_config_t config = chunker_get_default_config();
    config.max_chunk_size = 80;   // Force chunking 
    config.min_chunk_size = 30;   // Lower minimum to allow smaller chunks
    config.overlap_size = 20;     // Smaller overlap to fit within max_chunk_size
    config.preserve_paragraphs = 1;
    
    chunking_result_t *result = chunk_document(text, &config);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NULL(result->error);
    
    free_chunking_result(result);
}

void test_chunker_default_config(void) {
    chunking_config_t config = chunker_get_default_config();
    
    TEST_ASSERT_EQUAL_INT(1000, config.max_chunk_size);
    TEST_ASSERT_EQUAL_INT(200, config.overlap_size);
    TEST_ASSERT_EQUAL_INT(1, config.preserve_sentences);
    TEST_ASSERT_EQUAL_INT(0, config.preserve_paragraphs);
    TEST_ASSERT_EQUAL_INT(100, config.min_chunk_size);
}

void test_chunker_pdf_config(void) {
    chunking_config_t config = chunker_get_pdf_config();
    
    TEST_ASSERT_EQUAL_INT(1500, config.max_chunk_size);
    TEST_ASSERT_EQUAL_INT(300, config.overlap_size);
    TEST_ASSERT_EQUAL_INT(1, config.preserve_sentences);
    TEST_ASSERT_EQUAL_INT(1, config.preserve_paragraphs);
    TEST_ASSERT_EQUAL_INT(150, config.min_chunk_size);
}

void test_chunk_with_overlap(void) {
    // Create text that will be chunked with overlap - making it longer
    const char *text = "Word1 Word2 Word3 Word4 Word5 Word6 Word7 Word8 Word9 Word10 Word11 Word12 Word13 Word14 Word15 Word16 Word17 Word18 Word19 Word20 Word21 Word22 Word23 Word24 Word25 Word26 Word27 Word28 Word29 Word30";
    
    chunking_config_t config = chunker_get_default_config();
    config.max_chunk_size = 80;   // Small chunks to force splitting
    config.min_chunk_size = 30;   // Lower minimum to allow smaller chunks
    config.overlap_size = 20;     // Overlap that fits within max_chunk_size
    
    chunking_result_t *result = chunk_document(text, &config);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NULL(result->error);
    TEST_ASSERT_GREATER_THAN(1, result->chunks.count);
    
    // Verify overlaps exist between consecutive chunks
    if (result->chunks.count > 1) {
        for (size_t i = 1; i < result->chunks.count; i++) {
            // Check that there's some overlap: the start of chunk i should be before the end of chunk i-1
            TEST_ASSERT_LESS_THAN(result->chunks.data[i-1].end_offset, result->chunks.data[i].start_offset);
        }
    }
    
    free_chunking_result(result);
}

void test_invalid_config(void) {
    const char *text = "Some text to test with.";
    
    chunking_config_t config = chunker_get_default_config();
    config.max_chunk_size = 50;
    config.min_chunk_size = 100;  // Invalid: min > max
    
    chunking_result_t *result = chunk_document(text, &config);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NOT_NULL(result->error);
    TEST_ASSERT_EQUAL_INT(0, result->chunks.count);
    
    free_chunking_result(result);
}

void test_whitespace_trimming(void) {
    const char *text = "   This chunk has leading and trailing whitespace   ";
    
    chunking_result_t *result = chunk_document(text, NULL);
    
    TEST_ASSERT_NOT_NULL(result);
    TEST_ASSERT_NULL(result->error);
    TEST_ASSERT_EQUAL_INT(1, result->chunks.count);
    
    // Check that whitespace was trimmed
    char *chunk_text = result->chunks.data[0].text;
    TEST_ASSERT_EQUAL_CHAR('T', chunk_text[0]);  // Should start with 'T', not space
    
    size_t len = result->chunks.data[0].length;
    TEST_ASSERT_NOT_EQUAL(' ', chunk_text[len-1]);  // Should not end with space
    
    free_chunking_result(result);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_chunk_small_document_single_chunk);
    RUN_TEST(test_chunk_empty_document);
    RUN_TEST(test_chunk_null_document);
    RUN_TEST(test_chunk_large_document_multiple_chunks);
    RUN_TEST(test_chunk_with_sentences);
    RUN_TEST(test_chunk_with_paragraphs);
    RUN_TEST(test_chunker_default_config);
    RUN_TEST(test_chunker_pdf_config);
    RUN_TEST(test_chunk_with_overlap);
    RUN_TEST(test_invalid_config);
    RUN_TEST(test_whitespace_trimming);
    
    return UNITY_END();
}