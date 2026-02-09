#include "tool_cache.h"
#include <cJSON.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TOOL_CACHE_INITIAL_CAPACITY 16

static time_t get_file_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return st.st_mtime;
}

static char *extract_file_path(const char *arguments) {
    if (arguments == NULL) {
        return NULL;
    }

    cJSON *args = cJSON_Parse(arguments);
    if (args == NULL) {
        return NULL;
    }

    const char *keys[] = {"path", "file_path", "directory", NULL};
    char *result = NULL;

    for (int i = 0; keys[i] != NULL; i++) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(args, keys[i]);
        if (cJSON_IsString(item) && item->valuestring != NULL && item->valuestring[0] != '\0') {
            result = strdup(item->valuestring);
            break;
        }
    }

    cJSON_Delete(args);
    return result;
}

static void free_cache_entry(ToolCacheEntry *entry) {
    free(entry->tool_name);
    free(entry->arguments);
    free(entry->result);
    free(entry->file_path);
    entry->tool_name = NULL;
    entry->arguments = NULL;
    entry->result = NULL;
    entry->file_path = NULL;
}

ToolCache *tool_cache_create(void) {
    ToolCache *cache = calloc(1, sizeof(ToolCache));
    if (cache == NULL) {
        return NULL;
    }

    cache->entries = calloc(TOOL_CACHE_INITIAL_CAPACITY, sizeof(ToolCacheEntry));
    if (cache->entries == NULL) {
        free(cache);
        return NULL;
    }

    if (pthread_mutex_init(&cache->mutex, NULL) != 0) {
        free(cache->entries);
        free(cache);
        return NULL;
    }

    cache->count = 0;
    cache->capacity = TOOL_CACHE_INITIAL_CAPACITY;
    return cache;
}

void tool_cache_destroy(ToolCache *cache) {
    if (cache == NULL) {
        return;
    }

    for (int i = 0; i < cache->count; i++) {
        free_cache_entry(&cache->entries[i]);
    }
    free(cache->entries);
    pthread_mutex_destroy(&cache->mutex);
    free(cache);
}

ToolCacheEntry *tool_cache_lookup(ToolCache *cache, const char *tool_name, const char *arguments) {
    if (cache == NULL || tool_name == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&cache->mutex);

    const char *args = arguments ? arguments : "";

    for (int i = 0; i < cache->count; i++) {
        ToolCacheEntry *entry = &cache->entries[i];
        if (strcmp(entry->tool_name, tool_name) != 0) {
            continue;
        }
        if (strcmp(entry->arguments, args) != 0) {
            continue;
        }

        if (entry->file_path != NULL) {
            time_t current_mtime = get_file_mtime(entry->file_path);
            if (current_mtime != entry->file_mtime) {
                free_cache_entry(entry);
                if (i < cache->count - 1) {
                    cache->entries[i] = cache->entries[cache->count - 1];
                }
                cache->count--;
                pthread_mutex_unlock(&cache->mutex);
                return NULL;
            }
        }

        pthread_mutex_unlock(&cache->mutex);
        return entry;
    }

    pthread_mutex_unlock(&cache->mutex);
    return NULL;
}

int tool_cache_fetch(ToolCache *cache, const char *tool_name, const char *arguments,
                     char **out_result, int *out_success) {
    if (cache == NULL || tool_name == NULL || out_result == NULL || out_success == NULL) {
        return 0;
    }

    pthread_mutex_lock(&cache->mutex);

    const char *args = arguments ? arguments : "";

    for (int i = 0; i < cache->count; i++) {
        ToolCacheEntry *entry = &cache->entries[i];
        if (strcmp(entry->tool_name, tool_name) != 0) {
            continue;
        }
        if (strcmp(entry->arguments, args) != 0) {
            continue;
        }

        if (entry->file_path != NULL) {
            time_t current_mtime = get_file_mtime(entry->file_path);
            if (current_mtime != entry->file_mtime) {
                free_cache_entry(entry);
                if (i < cache->count - 1) {
                    cache->entries[i] = cache->entries[cache->count - 1];
                }
                cache->count--;
                pthread_mutex_unlock(&cache->mutex);
                return 0;
            }
        }

        *out_result = entry->result ? strdup(entry->result) : NULL;
        *out_success = entry->success;
        pthread_mutex_unlock(&cache->mutex);
        return 1;
    }

    pthread_mutex_unlock(&cache->mutex);
    return 0;
}

int tool_cache_store(ToolCache *cache, const char *tool_name, const char *arguments,
                     const char *result, int success) {
    if (cache == NULL || tool_name == NULL || result == NULL) {
        return -1;
    }

    pthread_mutex_lock(&cache->mutex);

    const char *args = arguments ? arguments : "";

    for (int i = 0; i < cache->count; i++) {
        ToolCacheEntry *existing = &cache->entries[i];
        if (strcmp(existing->tool_name, tool_name) == 0 &&
            strcmp(existing->arguments, args) == 0) {
            free(existing->result);
            existing->result = strdup(result);
            if (existing->result == NULL) {
                pthread_mutex_unlock(&cache->mutex);
                return -1;
            }
            existing->success = success;
            free(existing->file_path);
            existing->file_path = extract_file_path(arguments);
            existing->file_mtime = 0;
            if (existing->file_path != NULL) {
                existing->file_mtime = get_file_mtime(existing->file_path);
            }
            pthread_mutex_unlock(&cache->mutex);
            return 0;
        }
    }

    if (cache->count >= cache->capacity) {
        int new_capacity = cache->capacity * 2;
        ToolCacheEntry *new_entries = realloc(cache->entries, new_capacity * sizeof(ToolCacheEntry));
        if (new_entries == NULL) {
            pthread_mutex_unlock(&cache->mutex);
            return -1;
        }
        cache->entries = new_entries;
        cache->capacity = new_capacity;
    }

    ToolCacheEntry *entry = &cache->entries[cache->count];
    entry->tool_name = strdup(tool_name);
    entry->arguments = strdup(args);
    entry->result = strdup(result);
    entry->success = success;

    if (entry->tool_name == NULL || entry->arguments == NULL || entry->result == NULL) {
        free(entry->tool_name);
        free(entry->arguments);
        free(entry->result);
        pthread_mutex_unlock(&cache->mutex);
        return -1;
    }

    entry->file_path = extract_file_path(arguments);
    entry->file_mtime = 0;
    if (entry->file_path != NULL) {
        entry->file_mtime = get_file_mtime(entry->file_path);
    }

    cache->count++;
    pthread_mutex_unlock(&cache->mutex);
    return 0;
}

void tool_cache_invalidate_path(ToolCache *cache, const char *path) {
    if (cache == NULL || path == NULL) {
        return;
    }

    pthread_mutex_lock(&cache->mutex);

    for (int i = cache->count - 1; i >= 0; i--) {
        if (cache->entries[i].file_path != NULL &&
            strcmp(cache->entries[i].file_path, path) == 0) {
            free_cache_entry(&cache->entries[i]);
            if (i < cache->count - 1) {
                cache->entries[i] = cache->entries[cache->count - 1];
            }
            cache->count--;
        }
    }

    pthread_mutex_unlock(&cache->mutex);
}

void tool_cache_clear(ToolCache *cache) {
    if (cache == NULL) {
        return;
    }

    pthread_mutex_lock(&cache->mutex);

    for (int i = 0; i < cache->count; i++) {
        free_cache_entry(&cache->entries[i]);
    }
    cache->count = 0;

    pthread_mutex_unlock(&cache->mutex);
}
