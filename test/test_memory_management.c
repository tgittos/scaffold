#include "../test/unity/unity.h"
#include "db/metadata_store.h"
#include "ui/memory_commands.h"
#include "agent/session.h"
#include "services/services.h"
#include "util/ralph_home.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static metadata_store_t* test_store = NULL;
static AgentSession g_session;

void setUp(void) {
    ralph_home_init(NULL);

    // Clean up any existing test data
    const char* home = getenv("HOME");
    if (home != NULL) {
        char cmd[512] = {0};  // Initialize to avoid valgrind warnings
        snprintf(cmd, sizeof(cmd), "rm -rf %s/.local/ralph/metadata/test_index 2>/dev/null", home);
        system(cmd);
    }

    test_store = metadata_store_create(NULL);
}

void tearDown(void) {
    if (test_store != NULL) {
        metadata_store_destroy(test_store);
        test_store = NULL;
    }

    // Clean up test data
    const char* home = getenv("HOME");
    if (home != NULL) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s/.local/ralph/metadata/test_index 2>/dev/null", home);
        system(cmd);
    }

    ralph_home_cleanup();
}

void test_metadata_store_create_and_destroy(void) {
    metadata_store_t* store = metadata_store_create("/tmp/test_metadata");
    TEST_ASSERT_NOT_NULL(store);

    metadata_store_destroy(store);
}

void test_metadata_store_save_and_get(void) {
    setUp();  // Ensure clean state

    metadata_store_t* store = test_store;
    TEST_ASSERT_NOT_NULL(store);

    ChunkMetadata metadata = {
        .chunk_id = 12345,
        .content = "This is test content",
        .index_name = "test_index",
        .type = "test",
        .source = "unit_test",
        .importance = "high",
        .timestamp = time(NULL),
        .custom_metadata = "{\"test\": true}"
    };

    // Save metadata
    int result = metadata_store_save(store, &metadata);
    TEST_ASSERT_EQUAL(0, result);

    // Retrieve metadata
    ChunkMetadata* retrieved = metadata_store_get(store, "test_index", 12345);
    TEST_ASSERT_NOT_NULL(retrieved);
    TEST_ASSERT_EQUAL(12345, retrieved->chunk_id);
    TEST_ASSERT_EQUAL_STRING("This is test content", retrieved->content);
    TEST_ASSERT_EQUAL_STRING("test", retrieved->type);
    TEST_ASSERT_EQUAL_STRING("unit_test", retrieved->source);
    TEST_ASSERT_EQUAL_STRING("high", retrieved->importance);

    metadata_store_free_chunk(retrieved);
}

void test_metadata_store_delete(void) {
    setUp();  // Ensure clean state

    metadata_store_t* store = test_store;
    TEST_ASSERT_NOT_NULL(store);

    ChunkMetadata metadata = {
        .chunk_id = 99999,
        .content = "Delete me",
        .index_name = "test_index",
        .type = "temp",
        .source = "test",
        .importance = "low",
        .timestamp = time(NULL),
        .custom_metadata = NULL
    };

    // Save metadata
    int result = metadata_store_save(store, &metadata);
    TEST_ASSERT_EQUAL(0, result);

    // Verify it exists
    ChunkMetadata* exists = metadata_store_get(store, "test_index", 99999);
    TEST_ASSERT_NOT_NULL(exists);
    metadata_store_free_chunk(exists);

    // Delete it
    result = metadata_store_delete(store, "test_index", 99999);
    TEST_ASSERT_EQUAL(0, result);

    // Verify it's gone
    ChunkMetadata* gone = metadata_store_get(store, "test_index", 99999);
    TEST_ASSERT_NULL(gone);
}

void test_metadata_store_list(void) {
    // Use a unique test index for this test to avoid conflicts
    const char* test_index = "test_index_list";

    // Clean up any existing data for this specific index
    const char* home = getenv("HOME");
    if (home != NULL) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s/.local/ralph/metadata/%s 2>/dev/null", home, test_index);
        system(cmd);
    }

    metadata_store_t* store = test_store;
    TEST_ASSERT_NOT_NULL(store);

    // Add multiple chunks
    for (size_t i = 1; i <= 3; i++) {
        char content[128];
        snprintf(content, sizeof(content), "Test content %zu", i);

        ChunkMetadata metadata = {
            .chunk_id = i * 100000 + i,  // Use unique IDs to avoid conflicts
            .content = content,
            .index_name = (char*)test_index,
            .type = "test",
            .source = "unit_test",
            .importance = "normal",
            .timestamp = time(NULL),
            .custom_metadata = NULL
        };

        int result = metadata_store_save(store, &metadata);
        TEST_ASSERT_EQUAL(0, result);
    }

    // List all chunks
    size_t count = 0;
    ChunkMetadata** chunks = metadata_store_list(store, test_index, &count);
    TEST_ASSERT_NOT_NULL(chunks);
    TEST_ASSERT_EQUAL(3, count);

    // Verify chunks
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_NOT_NULL(chunks[i]);
        TEST_ASSERT_NOT_NULL(chunks[i]->content);
    }

    metadata_store_free_chunks(chunks, count);

    // Clean up after test
    if (home != NULL) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s/.local/ralph/metadata/%s 2>/dev/null", home, test_index);
        system(cmd);
    }
}

void test_metadata_store_search(void) {
    setUp();  // Ensure clean state

    metadata_store_t* store = test_store;
    TEST_ASSERT_NOT_NULL(store);

    // Add chunks with different content
    ChunkMetadata metadata1 = {
        .chunk_id = 10001,
        .content = "The quick brown fox",
        .index_name = "test_index",
        .type = "animal",
        .source = "test",
        .importance = "normal",
        .timestamp = time(NULL),
        .custom_metadata = NULL
    };

    ChunkMetadata metadata2 = {
        .chunk_id = 10002,
        .content = "The lazy dog",
        .index_name = "test_index",
        .type = "animal",
        .source = "test",
        .importance = "normal",
        .timestamp = time(NULL),
        .custom_metadata = NULL
    };

    ChunkMetadata metadata3 = {
        .chunk_id = 10003,
        .content = "Programming is fun",
        .index_name = "test_index",
        .type = "tech",
        .source = "test",
        .importance = "normal",
        .timestamp = time(NULL),
        .custom_metadata = NULL
    };

    metadata_store_save(store, &metadata1);
    metadata_store_save(store, &metadata2);
    metadata_store_save(store, &metadata3);

    // Search for "animal" in type
    size_t count = 0;
    ChunkMetadata** results = metadata_store_search(store, "test_index", "animal", &count);
    TEST_ASSERT_NOT_NULL(results);
    TEST_ASSERT_EQUAL(2, count);
    metadata_store_free_chunks(results, count);

    // Search for "fox" in content
    results = metadata_store_search(store, "test_index", "fox", &count);
    TEST_ASSERT_NOT_NULL(results);
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(10001, results[0]->chunk_id);
    metadata_store_free_chunks(results, count);

    // Search for non-existent term
    results = metadata_store_search(store, "test_index", "nonexistent", &count);
    TEST_ASSERT_NULL(results);
    TEST_ASSERT_EQUAL(0, count);
}

void test_metadata_store_update(void) {
    setUp();  // Ensure clean state

    metadata_store_t* store = test_store;
    TEST_ASSERT_NOT_NULL(store);

    ChunkMetadata metadata = {
        .chunk_id = 55555,
        .content = "Original content",
        .index_name = "test_index",
        .type = "original",
        .source = "test",
        .importance = "low",
        .timestamp = time(NULL),
        .custom_metadata = NULL
    };

    // Save original
    int result = metadata_store_save(store, &metadata);
    TEST_ASSERT_EQUAL(0, result);

    // Update it
    metadata.content = "Updated content";
    metadata.type = "updated";
    metadata.importance = "high";

    result = metadata_store_update(store, &metadata);
    TEST_ASSERT_EQUAL(0, result);

    // Verify update
    ChunkMetadata* updated = metadata_store_get(store, "test_index", 55555);
    TEST_ASSERT_NOT_NULL(updated);
    TEST_ASSERT_EQUAL_STRING("Updated content", updated->content);
    TEST_ASSERT_EQUAL_STRING("updated", updated->type);
    TEST_ASSERT_EQUAL_STRING("high", updated->importance);

    metadata_store_free_chunk(updated);
}

void test_memory_command_parsing(void) {
    int result = process_memory_command("help", &g_session);
    TEST_ASSERT_EQUAL(0, result);
}

void test_memory_show_invalid_chunk_id(void) {
    // Non-numeric input should fail
    int result = process_memory_command("show abc", &g_session);
    TEST_ASSERT_EQUAL(-1, result);

    // Overflow value should fail (strtoull returns ERANGE)
    result = process_memory_command("show 99999999999999999999999999", &g_session);
    TEST_ASSERT_EQUAL(-1, result);

    // Empty chunk ID should fail
    result = process_memory_command("show", &g_session);
    TEST_ASSERT_EQUAL(-1, result);
}

void test_memory_edit_invalid_chunk_id(void) {
    // Non-numeric chunk ID should fail
    int result = process_memory_command("edit abc type test", &g_session);
    TEST_ASSERT_EQUAL(-1, result);

    // Overflow chunk ID should fail
    result = process_memory_command("edit 99999999999999999999999999 type test", &g_session);
    TEST_ASSERT_EQUAL(-1, result);
}

int main(void) {
    UNITY_BEGIN();

    // Metadata store tests
    RUN_TEST(test_metadata_store_create_and_destroy);
    RUN_TEST(test_metadata_store_save_and_get);
    RUN_TEST(test_metadata_store_delete);
    RUN_TEST(test_metadata_store_list);
    RUN_TEST(test_metadata_store_search);
    RUN_TEST(test_metadata_store_update);

    // Wire Services for memory command tests
    Services* svc = services_create_empty();
    svc->metadata_store = metadata_store_create(NULL);
    memset(&g_session, 0, sizeof(g_session));
    g_session.services = svc;

    RUN_TEST(test_memory_command_parsing);
    RUN_TEST(test_memory_show_invalid_chunk_id);
    RUN_TEST(test_memory_edit_invalid_chunk_id);

    services_destroy(svc);

    return UNITY_END();
}
