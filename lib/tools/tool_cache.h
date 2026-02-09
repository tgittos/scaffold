#ifndef TOOL_CACHE_H
#define TOOL_CACHE_H

#include <pthread.h>
#include <time.h>

typedef struct {
    char *tool_name;
    char *arguments;
    char *result;
    int success;
    char *file_path;
    time_t file_mtime;
} ToolCacheEntry;

typedef struct ToolCache {
    ToolCacheEntry *entries;
    int count;
    int capacity;
    pthread_mutex_t mutex;
} ToolCache;

ToolCache *tool_cache_create(void);
void tool_cache_destroy(ToolCache *cache);
ToolCacheEntry *tool_cache_lookup(ToolCache *cache, const char *tool_name, const char *arguments);

/**
 * Thread-safe cache fetch: copies result string and success flag while holding
 * the lock, avoiding the dangling-pointer problem of tool_cache_lookup().
 *
 * @return 1 on hit (out_result and out_success populated), 0 on miss.
 *         Caller must free *out_result.
 */
int tool_cache_fetch(ToolCache *cache, const char *tool_name, const char *arguments,
                     char **out_result, int *out_success);

int tool_cache_store(ToolCache *cache, const char *tool_name, const char *arguments,
                     const char *result, int success);
void tool_cache_invalidate_path(ToolCache *cache, const char *path);
void tool_cache_clear(ToolCache *cache);

#endif /* TOOL_CACHE_H */
