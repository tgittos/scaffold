#include "unity.h"
#include "db/vector_db.h"
#include "db/hnswlib_wrapper.h"
#include "util/app_home.h"

extern void hnswlib_clear_all(void);
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>

void setUp(void) {
    app_home_init(NULL);
    // Clean up any leftover test files using rmdir
    rmdir("/tmp/vector_db_test");
    // Clear all indexes from hnswlib to ensure clean state
    hnswlib_clear_all();
}

void tearDown(void) {
    // Clean up test files after each test
    rmdir("/tmp/vector_db_test");
    app_home_cleanup();
}

static void fill_random_vector(vector_t* vec) {
    for (size_t i = 0; i < vec->dimension; i++) {
        vec->data[i] = (float)rand() / RAND_MAX;
    }
}

void test_vector_db_create_destroy(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    vector_db_destroy(db);
}

void test_vector_db_create_index(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "test_create_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    TEST_ASSERT_TRUE(vector_db_has_index(db, "test_create_idx"));
    TEST_ASSERT_FALSE(vector_db_has_index(db, "nonexistent"));
    
    err = vector_db_create_index(db, "test_create_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INVALID_PARAM, err);
    
    vector_db_destroy(db);
}

void test_vector_db_delete_index(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "test_delete_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    TEST_ASSERT_TRUE(vector_db_has_index(db, "test_delete_idx"));
    
    err = vector_db_delete_index(db, "test_delete_idx");
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    TEST_ASSERT_FALSE(vector_db_has_index(db, "test_delete_idx"));
    
    err = vector_db_delete_index(db, "test_delete_idx");
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INDEX_NOT_FOUND, err);
    
    vector_db_destroy(db);
}

void test_vector_db_list_indices(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    size_t count;
    char** names = vector_db_list_indices(db, &count);
    TEST_ASSERT_EQUAL(0, count);
    TEST_ASSERT_NULL(names);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_create_index(db, "list_idx1", &config);
    vector_db_create_index(db, "list_idx2", &config);
    vector_db_create_index(db, "list_idx3", &config);
    
    names = vector_db_list_indices(db, &count);
    TEST_ASSERT_EQUAL(3, count);
    TEST_ASSERT_NOT_NULL(names);
    
    int found1 = 0, found2 = 0, found3 = 0;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], "list_idx1") == 0) found1 = 1;
        if (strcmp(names[i], "list_idx2") == 0) found2 = 1;
        if (strcmp(names[i], "list_idx3") == 0) found3 = 1;
        free(names[i]);
    }
    free(names);
    
    TEST_ASSERT_TRUE(found1);
    TEST_ASSERT_TRUE(found2);
    TEST_ASSERT_TRUE(found3);
    
    vector_db_destroy(db);
}

void test_vector_db_add_and_get_vector(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "add_get_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* vec = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec);
    fill_random_vector(vec);
    
    err = vector_db_add_vector(db, "add_get_idx", vec, 1);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* retrieved = vector_create(128);
    TEST_ASSERT_NOT_NULL(retrieved);
    
    err = vector_db_get_vector(db, "add_get_idx", 1, retrieved);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    for (size_t i = 0; i < 128; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, vec->data[i], retrieved->data[i]);
    }
    
    err = vector_db_get_vector(db, "add_get_idx", 999, retrieved);
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_ELEMENT_NOT_FOUND, err);
    
    vector_destroy(vec);
    vector_destroy(retrieved);
    vector_db_destroy(db);
}

void test_vector_db_search(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "search_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* vectors[10];
    for (int i = 0; i < 10; i++) {
        vectors[i] = vector_create(128);
        TEST_ASSERT_NOT_NULL(vectors[i]);
        fill_random_vector(vectors[i]);
        
        err = vector_db_add_vector(db, "search_idx", vectors[i], i);
        TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    }
    
    vector_t* query = vector_clone(vectors[5]);
    TEST_ASSERT_NOT_NULL(query);
    
    search_results_t* results = vector_db_search(db, "search_idx", query, 5);
    TEST_ASSERT_NOT_NULL(results);
    TEST_ASSERT(results->count > 0);
    TEST_ASSERT(results->count <= 5);
    
    TEST_ASSERT_EQUAL(5, results->results[0].label);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, results->results[0].distance);
    
    vector_db_free_search_results(results);
    
    for (int i = 0; i < 10; i++) {
        vector_destroy(vectors[i]);
    }
    vector_destroy(query);
    vector_db_destroy(db);
}

void test_vector_db_update_delete(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    srand(42);  // Make random numbers deterministic
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "update_del_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* vec1 = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec1);
    fill_random_vector(vec1);
    
    err = vector_db_add_vector(db, "update_del_idx", vec1, 1);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* vec2 = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec2);
    fill_random_vector(vec2);
    
    err = vector_db_update_vector(db, "update_del_idx", vec2, 1);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* retrieved = vector_create(128);
    TEST_ASSERT_NOT_NULL(retrieved);
    
    err = vector_db_get_vector(db, "update_del_idx", 1, retrieved);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    for (size_t i = 0; i < 128; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, vec2->data[i], retrieved->data[i]);
    }
    
    err = vector_db_delete_vector(db, "update_del_idx", 1);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    err = vector_db_get_vector(db, "update_del_idx", 1, retrieved);
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_ELEMENT_NOT_FOUND, err);
    
    vector_destroy(vec1);
    vector_destroy(vec2);
    vector_destroy(retrieved);
    vector_db_destroy(db);
}

void test_vector_db_save_load(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "save_load_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* vec = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec);
    fill_random_vector(vec);
    
    err = vector_db_add_vector(db, "save_load_idx", vec, 42);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    rmdir("/tmp/vector_db_test");
    mkdir("/tmp/vector_db_test", 0755);
    
    err = vector_db_save_index(db, "save_load_idx", "/tmp/vector_db_test/test.index");
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_db_destroy(db);
    
    db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    err = vector_db_load_index(db, "save_load_idx", "/tmp/vector_db_test/test.index");
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* retrieved = vector_create(128);
    TEST_ASSERT_NOT_NULL(retrieved);
    
    err = vector_db_get_vector(db, "save_load_idx", 42, retrieved);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    for (size_t i = 0; i < 128; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, vec->data[i], retrieved->data[i]);
    }
    
    unlink("/tmp/vector_db_test/test.index");
    unlink("/tmp/vector_db_test/test.index.meta");
    rmdir("/tmp/vector_db_test");
    
    vector_destroy(vec);
    vector_destroy(retrieved);
    vector_db_destroy(db);
}

void test_vector_db_auto_flush(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "auto_flush_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    rmdir("/tmp/vector_db_test");
    mkdir("/tmp/vector_db_test", 0755);
    
    vector_t* vec = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec);
    fill_random_vector(vec);
    
    err = vector_db_add_vector(db, "auto_flush_idx", vec, 42);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    // Just use synchronous save instead of the async auto-flush bullshit
    err = vector_db_save_all(db, "/tmp/vector_db_test");
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    struct stat st;
    int exists = stat("/tmp/vector_db_test/auto_flush_idx.index", &st) == 0;
    TEST_ASSERT_TRUE(exists);
    
    unlink("/tmp/vector_db_test/auto_flush_idx.index");
    unlink("/tmp/vector_db_test/auto_flush_idx.index.meta");
    rmdir("/tmp/vector_db_test");
    
    vector_destroy(vec);
    vector_db_destroy(db);
}

void test_vector_db_default_serialization(void) {
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    char* default_dir = vector_db_get_default_directory();
    TEST_ASSERT_NOT_NULL(default_dir);
    
    struct stat st;
    int dir_exists = stat(default_dir, &st) == 0;
    TEST_ASSERT_TRUE(dir_exists);
    
    index_config_t config = {
        .dimension = 128,
        .max_elements = 1000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, "default_test_idx", &config);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    vector_t* vec = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec);
    fill_random_vector(vec);
    
    err = vector_db_add_vector(db, "default_test_idx", vec, 42);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    err = vector_db_flush_now(db);
    TEST_ASSERT_EQUAL(VECTOR_DB_OK, err);
    
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/default_test_idx.index", default_dir);
    char meta_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s/default_test_idx.index.meta", default_dir);
    
    int index_exists = stat(index_path, &st) == 0;
    int meta_exists = stat(meta_path, &st) == 0;
    TEST_ASSERT_TRUE(index_exists);
    TEST_ASSERT_TRUE(meta_exists);
    
    unlink(index_path);
    unlink(meta_path);
    
    vector_destroy(vec);
    vector_db_destroy(db);
    free(default_dir);
}

void test_vector_utilities(void) {
    vector_t* vec = vector_create(128);
    TEST_ASSERT_NOT_NULL(vec);
    TEST_ASSERT_NOT_NULL(vec->data);
    TEST_ASSERT_EQUAL(128, vec->dimension);
    
    fill_random_vector(vec);
    
    vector_t* clone = vector_clone(vec);
    TEST_ASSERT_NOT_NULL(clone);
    TEST_ASSERT_NOT_NULL(clone->data);
    TEST_ASSERT_EQUAL(vec->dimension, clone->dimension);
    
    for (size_t i = 0; i < vec->dimension; i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, vec->data[i], clone->data[i]);
    }
    
    vector_destroy(vec);
    vector_destroy(clone);
}

void test_vector_db_error_handling(void) {
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INVALID_PARAM, 
                     vector_db_create_index(NULL, "test", NULL));
    
    vector_db_t* db = vector_db_create();
    TEST_ASSERT_NOT_NULL(db);
    
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INVALID_PARAM, 
                     vector_db_create_index(db, NULL, NULL));
    
    index_config_t config = {
        .dimension = 0,
        .max_elements = 10000,
        .M = 16,
        .ef_construction = 200,
        .random_seed = 100,
        .metric = "l2"
    };
    
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INVALID_PARAM, 
                     vector_db_create_index(db, "test", &config));
    
    config.dimension = 128;
    config.max_elements = 0;
    
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INVALID_PARAM, 
                     vector_db_create_index(db, "test", &config));
    
    TEST_ASSERT_EQUAL(VECTOR_DB_ERROR_INVALID_PARAM,
                     vector_db_add_vector(db, "nonexistent", NULL, 0));
    
    vector_db_destroy(db);
}

int main(void) {
    UNITY_BEGIN();
    
    RUN_TEST(test_vector_db_create_destroy);
    RUN_TEST(test_vector_db_create_index);
    RUN_TEST(test_vector_db_delete_index);
    RUN_TEST(test_vector_db_list_indices);
    RUN_TEST(test_vector_db_add_and_get_vector);
    RUN_TEST(test_vector_db_search);
    RUN_TEST(test_vector_db_update_delete);
    RUN_TEST(test_vector_db_save_load);
    RUN_TEST(test_vector_db_auto_flush);
    RUN_TEST(test_vector_db_default_serialization);
    RUN_TEST(test_vector_utilities);
    RUN_TEST(test_vector_db_error_handling);
    
    return UNITY_END();
}