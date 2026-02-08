#include "unity/unity.h"
#include "lib/tools/tool_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static ToolCache *cache;
static char temp_file[256] = {0};
static int temp_file_created;

void setUp(void) {
    cache = tool_cache_create();
    temp_file_created = 0;
}

void tearDown(void) {
    tool_cache_destroy(cache);
    cache = NULL;
    if (temp_file_created) {
        unlink(temp_file);
        temp_file_created = 0;
    }
}

static void create_temp_file(void) {
    snprintf(temp_file, sizeof(temp_file), "/tmp/test_tool_cache_XXXXXX");
    int fd = mkstemp(temp_file);
    TEST_ASSERT_NOT_EQUAL(-1, fd);
    write(fd, "hello", 5);
    close(fd);
    temp_file_created = 1;
}

void test_create_destroy(void) {
    TEST_ASSERT_NOT_NULL(cache);
}

void test_store_then_lookup(void) {
    int rc = tool_cache_store(cache, "read_file", "{\"path\":\"/tmp/nonexistent\"}", "file contents", 1);
    TEST_ASSERT_EQUAL_INT(0, rc);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "read_file", "{\"path\":\"/tmp/nonexistent\"}");
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_STRING("file contents", hit->result);
    TEST_ASSERT_EQUAL_INT(1, hit->success);
}

void test_lookup_miss(void) {
    tool_cache_store(cache, "read_file", "{\"path\":\"/a\"}", "contents", 1);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "read_file", "{\"path\":\"/b\"}");
    TEST_ASSERT_NULL(hit);
}

void test_lookup_miss_different_tool(void) {
    tool_cache_store(cache, "read_file", "{\"path\":\"/a\"}", "contents", 1);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "list_dir", "{\"path\":\"/a\"}");
    TEST_ASSERT_NULL(hit);
}

void test_mtime_invalidation(void) {
    create_temp_file();

    char args[512] = {0};
    snprintf(args, sizeof(args), "{\"path\":\"%s\"}", temp_file);

    tool_cache_store(cache, "read_file", args, "original", 1);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "read_file", args);
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_STRING("original", hit->result);

    /* Touch the file to change mtime — sleep 1s to ensure different mtime */
    sleep(1);
    FILE *f = fopen(temp_file, "w");
    TEST_ASSERT_NOT_NULL(f);
    fprintf(f, "changed");
    fclose(f);

    hit = tool_cache_lookup(cache, "read_file", args);
    TEST_ASSERT_NULL(hit);
}

void test_explicit_path_invalidation(void) {
    tool_cache_store(cache, "read_file", "{\"path\":\"/foo/bar.txt\"}", "contents", 1);
    tool_cache_store(cache, "list_dir", "{\"path\":\"/foo\"}", "listing", 1);

    tool_cache_invalidate_path(cache, "/foo/bar.txt");

    ToolCacheEntry *hit1 = tool_cache_lookup(cache, "read_file", "{\"path\":\"/foo/bar.txt\"}");
    TEST_ASSERT_NULL(hit1);

    ToolCacheEntry *hit2 = tool_cache_lookup(cache, "list_dir", "{\"path\":\"/foo\"}");
    TEST_ASSERT_NOT_NULL(hit2);
    TEST_ASSERT_EQUAL_STRING("listing", hit2->result);
}

void test_clear(void) {
    tool_cache_store(cache, "a", "{}", "r1", 1);
    tool_cache_store(cache, "b", "{}", "r2", 1);

    tool_cache_clear(cache);

    TEST_ASSERT_NULL(tool_cache_lookup(cache, "a", "{}"));
    TEST_ASSERT_NULL(tool_cache_lookup(cache, "b", "{}"));
}

void test_null_arguments(void) {
    int rc = tool_cache_store(cache, "recall_memories", NULL, "memories", 1);
    TEST_ASSERT_EQUAL_INT(0, rc);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "recall_memories", NULL);
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_STRING("memories", hit->result);
}

void test_empty_arguments(void) {
    int rc = tool_cache_store(cache, "recall_memories", "", "memories", 1);
    TEST_ASSERT_EQUAL_INT(0, rc);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "recall_memories", "");
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_STRING("memories", hit->result);
}

void test_multiple_entries(void) {
    tool_cache_store(cache, "read_file", "{\"path\":\"/a\"}", "contents_a", 1);
    tool_cache_store(cache, "read_file", "{\"path\":\"/b\"}", "contents_b", 1);
    tool_cache_store(cache, "list_dir", "{\"path\":\"/c\"}", "listing_c", 1);

    ToolCacheEntry *a = tool_cache_lookup(cache, "read_file", "{\"path\":\"/a\"}");
    ToolCacheEntry *b = tool_cache_lookup(cache, "read_file", "{\"path\":\"/b\"}");
    ToolCacheEntry *c = tool_cache_lookup(cache, "list_dir", "{\"path\":\"/c\"}");

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_STRING("contents_a", a->result);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_STRING("contents_b", b->result);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("listing_c", c->result);
}

void test_capacity_growth(void) {
    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "tool_%d", i);
        int rc = tool_cache_store(cache, name, "{}", "result", 1);
        TEST_ASSERT_EQUAL_INT(0, rc);
    }

    for (int i = 0; i < 50; i++) {
        char name[32];
        snprintf(name, sizeof(name), "tool_%d", i);
        ToolCacheEntry *hit = tool_cache_lookup(cache, name, "{}");
        TEST_ASSERT_NOT_NULL(hit);
    }
}

void test_null_cache_operations(void) {
    TEST_ASSERT_NULL(tool_cache_lookup(NULL, "x", "{}"));
    TEST_ASSERT_EQUAL_INT(-1, tool_cache_store(NULL, "x", "{}", "r", 1));
    tool_cache_invalidate_path(NULL, "/x");
    tool_cache_clear(NULL);
    tool_cache_destroy(NULL);
}

void test_store_failure_result(void) {
    int rc = tool_cache_store(cache, "read_file", "{\"path\":\"/missing\"}", "error: not found", 0);
    TEST_ASSERT_EQUAL_INT(0, rc);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "read_file", "{\"path\":\"/missing\"}");
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_INT(0, hit->success);
    TEST_ASSERT_EQUAL_STRING("error: not found", hit->result);
}

void test_overwrite_existing_entry(void) {
    tool_cache_store(cache, "read_file", "{\"path\":\"/a\"}", "old_result", 1);
    tool_cache_store(cache, "read_file", "{\"path\":\"/a\"}", "new_result", 1);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "read_file", "{\"path\":\"/a\"}");
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_STRING("new_result", hit->result);
}

void test_file_path_extraction(void) {
    create_temp_file();

    char args[512] = {0};
    snprintf(args, sizeof(args), "{\"file_path\":\"%s\"}", temp_file);

    tool_cache_store(cache, "some_tool", args, "result", 1);

    ToolCacheEntry *hit = tool_cache_lookup(cache, "some_tool", args);
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_STRING(temp_file, hit->file_path);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_destroy);
    RUN_TEST(test_store_then_lookup);
    RUN_TEST(test_lookup_miss);
    RUN_TEST(test_lookup_miss_different_tool);
    RUN_TEST(test_mtime_invalidation);
    RUN_TEST(test_explicit_path_invalidation);
    RUN_TEST(test_clear);
    RUN_TEST(test_null_arguments);
    RUN_TEST(test_empty_arguments);
    RUN_TEST(test_multiple_entries);
    RUN_TEST(test_capacity_growth);
    RUN_TEST(test_null_cache_operations);
    RUN_TEST(test_store_failure_result);
    RUN_TEST(test_overwrite_existing_entry);
    RUN_TEST(test_file_path_extraction);
    return UNITY_END();
}
