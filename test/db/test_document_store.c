#include "../unity/unity.h"
#include "../../src/db/document_store.h"
#include "../../src/db/vector_db_service.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void setUp(void) {
    // Setup code
}

void tearDown(void) {
    // Cleanup code
}

void test_document_store_create_and_destroy(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    document_store_destroy(store);
}

void test_document_store_singleton(void) {
    document_store_t* store1 = document_store_get_instance();
    TEST_ASSERT_NOT_NULL(store1);
    
    document_store_t* store2 = document_store_get_instance();
    TEST_ASSERT_EQUAL_PTR(store1, store2);
}

void test_document_store_add_and_get(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    const char* index_name = "test_index";
    const char* content = "This is test content for document store";
    float embedding[128];
    for (int i = 0; i < 128; i++) {
        embedding[i] = (float)i / 128.0f;
    }
    
    // Ensure index exists
    int result = document_store_ensure_index(store, index_name, 128, 1000);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Add document
    result = document_store_add(store, index_name, content, embedding, 128, 
                               "test", "unit_test", "{\"test\": true}");
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Get document
    document_t* doc = document_store_get(store, index_name, 0);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_STRING(content, doc->content);
    TEST_ASSERT_EQUAL_STRING("test", doc->type);
    TEST_ASSERT_EQUAL_STRING("unit_test", doc->source);
    TEST_ASSERT_NOT_NULL(doc->metadata_json);
    
    document_store_free_document(doc);
    document_store_destroy(store);
}

void test_document_store_search(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    const char* index_name = "search_test_index";
    
    // Ensure index exists
    int result = document_store_ensure_index(store, index_name, 128, 1000);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Add multiple documents
    for (int i = 0; i < 3; i++) {
        char content[100];
        snprintf(content, sizeof(content), "Document %d content", i);
        
        float embedding[128];
        for (int j = 0; j < 128; j++) {
            embedding[j] = (float)(i + j) / 128.0f;
        }
        
        result = document_store_add(store, index_name, content, embedding, 128,
                                   "test", "unit_test", NULL);
        TEST_ASSERT_EQUAL_INT(0, result);
    }
    
    // Search for documents
    float query_embedding[128];
    for (int i = 0; i < 128; i++) {
        query_embedding[i] = (float)i / 128.0f;
    }
    
    document_search_results_t* results = document_store_search(store, index_name,
                                                              query_embedding, 128, 2);
    TEST_ASSERT_NOT_NULL(results);
    TEST_ASSERT_TRUE(results->results.count > 0);
    TEST_ASSERT_TRUE(results->results.count <= 2);

    // Check that we got documents back
    for (size_t i = 0; i < results->results.count; i++) {
        TEST_ASSERT_NOT_NULL(results->results.data[i].document);
        TEST_ASSERT_NOT_NULL(results->results.data[i].document->content);
        TEST_ASSERT_TRUE(strstr(results->results.data[i].document->content, "Document") != NULL);
    }

    document_store_free_results(results);
    document_store_destroy(store);
}

void test_document_store_search_by_time(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    const char* index_name = "time_test_index";
    time_t start_time = time(NULL);
    
    // Ensure index exists
    int result = document_store_ensure_index(store, index_name, 128, 1000);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Add documents
    for (int i = 0; i < 3; i++) {
        char content[100];
        snprintf(content, sizeof(content), "Time document %d", i);
        
        float embedding[128];
        for (int j = 0; j < 128; j++) {
            embedding[j] = (float)(i + j) / 128.0f;
        }
        
        result = document_store_add(store, index_name, content, embedding, 128,
                                   "test", "unit_test", NULL);
        TEST_ASSERT_EQUAL_INT(0, result);
        
        usleep(100000); // Sleep 100ms between documents
    }
    
    time_t end_time = time(NULL) + 1;
    
    // Search by time range
    document_search_results_t* results = document_store_search_by_time(store, index_name,
                                                                      start_time, end_time, 10);
    TEST_ASSERT_NOT_NULL(results);
    TEST_ASSERT_EQUAL_INT(3, results->results.count);

    for (size_t i = 0; i < results->results.count; i++) {
        TEST_ASSERT_NOT_NULL(results->results.data[i].document);
        TEST_ASSERT_TRUE(results->results.data[i].document->timestamp >= start_time);
        TEST_ASSERT_TRUE(results->results.data[i].document->timestamp <= end_time);
    }

    document_store_free_results(results);
    document_store_destroy(store);
}

void test_document_store_update(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    const char* index_name = "update_test_index";
    const char* original_content = "Original content";
    const char* updated_content = "Updated content";
    
    float embedding[128];
    for (int i = 0; i < 128; i++) {
        embedding[i] = (float)i / 128.0f;
    }
    
    // Ensure index exists
    int result = document_store_ensure_index(store, index_name, 128, 1000);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Add document
    result = document_store_add(store, index_name, original_content, embedding, 128,
                               "test", "unit_test", NULL);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Update document
    float new_embedding[128];
    for (int i = 0; i < 128; i++) {
        new_embedding[i] = (float)(128 - i) / 128.0f;
    }
    
    result = document_store_update(store, index_name, 0, updated_content, 
                                  new_embedding, 128, "{\"updated\": true}");
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Get updated document
    document_t* doc = document_store_get(store, index_name, 0);
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_EQUAL_STRING(updated_content, doc->content);
    TEST_ASSERT_NOT_NULL(doc->metadata_json);
    TEST_ASSERT_TRUE(strstr(doc->metadata_json, "updated") != NULL);
    
    document_store_free_document(doc);
    document_store_destroy(store);
}

void test_document_store_delete(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    const char* index_name = "delete_test_index";
    const char* content = "Document to delete";
    
    float embedding[128];
    for (int i = 0; i < 128; i++) {
        embedding[i] = (float)i / 128.0f;
    }
    
    // Ensure index exists
    int result = document_store_ensure_index(store, index_name, 128, 1000);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Add document
    result = document_store_add(store, index_name, content, embedding, 128,
                               "test", "unit_test", NULL);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Verify document exists
    document_t* doc = document_store_get(store, index_name, 0);
    TEST_ASSERT_NOT_NULL(doc);
    document_store_free_document(doc);
    
    // Delete document
    result = document_store_delete(store, index_name, 0);
    TEST_ASSERT_EQUAL_INT(0, result);
    
    // Verify document is deleted (should return NULL)
    doc = document_store_get(store, index_name, 0);
    TEST_ASSERT_NULL(doc);
    
    document_store_destroy(store);
}

void test_document_store_list_indices(void) {
    document_store_t* store = document_store_create("/tmp/test_doc_store");
    TEST_ASSERT_NOT_NULL(store);
    
    // Create multiple indices
    const char* indices[] = {"index1", "index2", "index3"};
    for (int i = 0; i < 3; i++) {
        int result = document_store_ensure_index(store, indices[i], 128, 1000);
        TEST_ASSERT_EQUAL_INT(0, result);
    }
    
    // List indices
    size_t count = 0;
    char** index_list = document_store_list_indices(store, &count);
    TEST_ASSERT_NOT_NULL(index_list);
    TEST_ASSERT_TRUE(count >= 3);
    
    // Check that our indices are in the list
    for (int i = 0; i < 3; i++) {
        int found = 0;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(index_list[j], indices[i]) == 0) {
                found = 1;
                break;
            }
        }
        TEST_ASSERT_TRUE(found);
    }
    
    // Free the list
    for (size_t i = 0; i < count; i++) {
        free(index_list[i]);
    }
    free(index_list);
    
    document_store_destroy(store);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_document_store_create_and_destroy);
    RUN_TEST(test_document_store_singleton);
    RUN_TEST(test_document_store_add_and_get);
    RUN_TEST(test_document_store_search);
    RUN_TEST(test_document_store_search_by_time);
    RUN_TEST(test_document_store_update);
    RUN_TEST(test_document_store_delete);
    RUN_TEST(test_document_store_list_indices);
    
    return UNITY_END();
}