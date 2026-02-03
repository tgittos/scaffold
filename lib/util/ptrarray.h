/**
 * ptrarray.h - Type-safe dynamic pointer array with ownership semantics
 *
 * A specialized dynamic array for storing pointers to heap-allocated objects.
 * Supports optional ownership: when enabled, the array will call a destructor
 * function on each element during cleanup.
 *
 * This is optimized for the common pattern in this codebase:
 *   - LLMProvider** providers
 *   - Task** tasks
 *   - char** strings
 *   - document_t** documents
 *
 * Usage:
 *   // Declare a pointer array type
 *   PTRARRAY_DECLARE(TaskArray, Task)
 *
 *   // Define the implementation
 *   PTRARRAY_DEFINE(TaskArray, Task)
 *
 *   // Use it:
 *   TaskArray arr;
 *   TaskArray_init(&arr, task_free);  // or NULL for non-owning
 *   TaskArray_push(&arr, task);
 *   Task* t = TaskArray_get(&arr, 0);
 *   TaskArray_destroy(&arr);          // calls task_free on each element
 *
 * For string arrays (char**), a convenience type is pre-defined:
 *   StringArray arr;
 *   StringArray_init(&arr, free);     // owns strings
 *   StringArray_push(&arr, strdup("hello"));
 *   StringArray_destroy(&arr);
 */

#ifndef PTRARRAY_H
#define PTRARRAY_H

#include <stdlib.h>
#include <string.h>

/* Default initial capacity */
#define PTRARRAY_DEFAULT_CAPACITY 8

/* Growth factor */
#define PTRARRAY_GROWTH_FACTOR 2

/**
 * PTRARRAY_DECLARE - Declare a pointer array type
 *
 * @param NAME      The name of the array type (e.g., TaskArray)
 * @param ELEMTYPE  The pointed-to type (e.g., Task, NOT Task*)
 */
#define PTRARRAY_DECLARE(NAME, ELEMTYPE)                                      \
    typedef void (*NAME##_destructor_fn)(ELEMTYPE*);                          \
                                                                              \
    typedef struct NAME {                                                     \
        ELEMTYPE** data;                                                      \
        size_t count;                                                         \
        size_t capacity;                                                      \
        NAME##_destructor_fn destructor;                                      \
    } NAME;                                                                   \
                                                                              \
    int NAME##_init(NAME* arr, NAME##_destructor_fn destructor);              \
    int NAME##_init_capacity(NAME* arr, size_t cap, NAME##_destructor_fn d);  \
    void NAME##_destroy(NAME* arr);                                           \
    void NAME##_destroy_shallow(NAME* arr);                                   \
    int NAME##_push(NAME* arr, ELEMTYPE* item);                               \
    ELEMTYPE* NAME##_pop(NAME* arr);                                          \
    ELEMTYPE* NAME##_get(const NAME* arr, size_t index);                      \
    int NAME##_set(NAME* arr, size_t index, ELEMTYPE* item);                  \
    int NAME##_insert(NAME* arr, size_t index, ELEMTYPE* item);               \
    ELEMTYPE* NAME##_remove(NAME* arr, size_t index);                         \
    void NAME##_delete(NAME* arr, size_t index);                              \
    void NAME##_clear(NAME* arr);                                             \
    void NAME##_clear_shallow(NAME* arr);                                     \
    int NAME##_shrink(NAME* arr);                                             \
    int NAME##_reserve(NAME* arr, size_t min_capacity);                       \
    ELEMTYPE** NAME##_steal(NAME* arr, size_t* out_count);                    \
    int NAME##_find(const NAME* arr, const ELEMTYPE* item);

/**
 * PTRARRAY_DEFINE - Define the function implementations
 */
#define PTRARRAY_DEFINE(NAME, ELEMTYPE)                                       \
                                                                              \
int NAME##_init(NAME* arr, NAME##_destructor_fn destructor) {                 \
    return NAME##_init_capacity(arr, PTRARRAY_DEFAULT_CAPACITY, destructor);  \
}                                                                             \
                                                                              \
int NAME##_init_capacity(NAME* arr, size_t cap, NAME##_destructor_fn d) {     \
    if (arr == NULL) return -1;                                               \
    if (cap == 0) cap = PTRARRAY_DEFAULT_CAPACITY;                            \
    arr->data = malloc(sizeof(ELEMTYPE*) * cap);                              \
    if (arr->data == NULL) return -1;                                         \
    arr->count = 0;                                                           \
    arr->capacity = cap;                                                      \
    arr->destructor = d;                                                      \
    return 0;                                                                 \
}                                                                             \
                                                                              \
void NAME##_destroy(NAME* arr) {                                              \
    if (arr == NULL) return;                                                  \
    if (arr->destructor != NULL) {                                            \
        for (size_t i = 0; i < arr->count; i++) {                             \
            if (arr->data[i] != NULL) {                                       \
                arr->destructor(arr->data[i]);                                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    free(arr->data);                                                          \
    arr->data = NULL;                                                         \
    arr->count = 0;                                                           \
    arr->capacity = 0;                                                        \
    arr->destructor = NULL;                                                   \
}                                                                             \
                                                                              \
void NAME##_destroy_shallow(NAME* arr) {                                      \
    if (arr == NULL) return;                                                  \
    free(arr->data);                                                          \
    arr->data = NULL;                                                         \
    arr->count = 0;                                                           \
    arr->capacity = 0;                                                        \
    arr->destructor = NULL;                                                   \
}                                                                             \
                                                                              \
static int NAME##_grow(NAME* arr) {                                           \
    if (arr == NULL) return -1;                                               \
    size_t new_cap = arr->capacity * PTRARRAY_GROWTH_FACTOR;                  \
    if (new_cap == 0) new_cap = PTRARRAY_DEFAULT_CAPACITY;                    \
    ELEMTYPE** new_data = realloc(arr->data, sizeof(ELEMTYPE*) * new_cap);    \
    if (new_data == NULL) return -1;                                          \
    arr->data = new_data;                                                     \
    arr->capacity = new_cap;                                                  \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_push(NAME* arr, ELEMTYPE* item) {                                  \
    if (arr == NULL) return -1;                                               \
    if (arr->count >= arr->capacity) {                                        \
        if (NAME##_grow(arr) != 0) return -1;                                 \
    }                                                                         \
    arr->data[arr->count] = item;                                             \
    arr->count++;                                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
ELEMTYPE* NAME##_pop(NAME* arr) {                                             \
    if (arr == NULL || arr->count == 0) return NULL;                          \
    arr->count--;                                                             \
    return arr->data[arr->count];                                             \
}                                                                             \
                                                                              \
ELEMTYPE* NAME##_get(const NAME* arr, size_t index) {                         \
    if (arr == NULL || index >= arr->count) return NULL;                      \
    return arr->data[index];                                                  \
}                                                                             \
                                                                              \
int NAME##_set(NAME* arr, size_t index, ELEMTYPE* item) {                     \
    if (arr == NULL || index >= arr->count) return -1;                        \
    if (arr->destructor != NULL && arr->data[index] != NULL) {                \
        arr->destructor(arr->data[index]);                                    \
    }                                                                         \
    arr->data[index] = item;                                                  \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_insert(NAME* arr, size_t index, ELEMTYPE* item) {                  \
    if (arr == NULL || index > arr->count) return -1;                         \
    if (arr->count >= arr->capacity) {                                        \
        if (NAME##_grow(arr) != 0) return -1;                                 \
    }                                                                         \
    if (index < arr->count) {                                                 \
        memmove(&arr->data[index + 1], &arr->data[index],                     \
                (arr->count - index) * sizeof(ELEMTYPE*));                    \
    }                                                                         \
    arr->data[index] = item;                                                  \
    arr->count++;                                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
ELEMTYPE* NAME##_remove(NAME* arr, size_t index) {                            \
    if (arr == NULL || index >= arr->count) return NULL;                      \
    ELEMTYPE* item = arr->data[index];                                        \
    if (index < arr->count - 1) {                                             \
        memmove(&arr->data[index], &arr->data[index + 1],                     \
                (arr->count - index - 1) * sizeof(ELEMTYPE*));                \
    }                                                                         \
    arr->count--;                                                             \
    return item;                                                              \
}                                                                             \
                                                                              \
void NAME##_delete(NAME* arr, size_t index) {                                 \
    ELEMTYPE* item = NAME##_remove(arr, index);                               \
    if (item != NULL && arr->destructor != NULL) {                            \
        arr->destructor(item);                                                \
    }                                                                         \
}                                                                             \
                                                                              \
void NAME##_clear(NAME* arr) {                                                \
    if (arr == NULL) return;                                                  \
    if (arr->destructor != NULL) {                                            \
        for (size_t i = 0; i < arr->count; i++) {                             \
            if (arr->data[i] != NULL) {                                       \
                arr->destructor(arr->data[i]);                                \
            }                                                                 \
        }                                                                     \
    }                                                                         \
    arr->count = 0;                                                           \
}                                                                             \
                                                                              \
void NAME##_clear_shallow(NAME* arr) {                                        \
    if (arr == NULL) return;                                                  \
    arr->count = 0;                                                           \
}                                                                             \
                                                                              \
int NAME##_shrink(NAME* arr) {                                                \
    if (arr == NULL || arr->count == 0) return -1;                            \
    if (arr->count == arr->capacity) return 0;                                \
    ELEMTYPE** new_data = realloc(arr->data, sizeof(ELEMTYPE*) * arr->count); \
    if (new_data == NULL) return -1;                                          \
    arr->data = new_data;                                                     \
    arr->capacity = arr->count;                                               \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_reserve(NAME* arr, size_t min_capacity) {                          \
    if (arr == NULL) return -1;                                               \
    if (min_capacity <= arr->capacity) return 0;                              \
    ELEMTYPE** new_data = realloc(arr->data, sizeof(ELEMTYPE*) * min_capacity);\
    if (new_data == NULL) return -1;                                          \
    arr->data = new_data;                                                     \
    arr->capacity = min_capacity;                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
ELEMTYPE** NAME##_steal(NAME* arr, size_t* out_count) {                       \
    if (arr == NULL) return NULL;                                             \
    ELEMTYPE** data = arr->data;                                              \
    if (out_count != NULL) *out_count = arr->count;                           \
    arr->data = NULL;                                                         \
    arr->count = 0;                                                           \
    arr->capacity = 0;                                                        \
    return data;                                                              \
}                                                                             \
                                                                              \
int NAME##_find(const NAME* arr, const ELEMTYPE* item) {                      \
    if (arr == NULL) return -1;                                               \
    for (size_t i = 0; i < arr->count; i++) {                                 \
        if (arr->data[i] == item) return (int)i;                              \
    }                                                                         \
    return -1;                                                                \
}

/**
 * PTRARRAY_IMPL - Convenience macro for header-only usage
 */
#define PTRARRAY_IMPL(NAME, ELEMTYPE) \
    PTRARRAY_DECLARE(NAME, ELEMTYPE)  \
    PTRARRAY_DEFINE(NAME, ELEMTYPE)

/*
 * Pre-defined string array type (char**)
 * This is commonly needed and avoids repetitive declarations.
 * Uses void* destructor to be compatible with free().
 */
typedef void (*StringArray_destructor_fn)(void*);

typedef struct StringArray {
    char** data;
    size_t count;
    size_t capacity;
    StringArray_destructor_fn destructor;
} StringArray;

static inline int StringArray_init(StringArray* arr, StringArray_destructor_fn destructor) {
    if (arr == NULL) return -1;
    arr->data = malloc(sizeof(char*) * PTRARRAY_DEFAULT_CAPACITY);
    if (arr->data == NULL) return -1;
    arr->count = 0;
    arr->capacity = PTRARRAY_DEFAULT_CAPACITY;
    arr->destructor = destructor;
    return 0;
}

static inline void StringArray_destroy(StringArray* arr) {
    if (arr == NULL) return;
    if (arr->destructor != NULL) {
        for (size_t i = 0; i < arr->count; i++) {
            if (arr->data[i] != NULL) {
                arr->destructor(arr->data[i]);
            }
        }
    }
    free(arr->data);
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static inline int StringArray_push(StringArray* arr, char* item) {
    if (arr == NULL) return -1;
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity * PTRARRAY_GROWTH_FACTOR;
        char** new_data = realloc(arr->data, sizeof(char*) * new_cap);
        if (new_data == NULL) return -1;
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->count] = item;
    arr->count++;
    return 0;
}

static inline char* StringArray_get(const StringArray* arr, size_t index) {
    if (arr == NULL || index >= arr->count) return NULL;
    return arr->data[index];
}

static inline char* StringArray_pop(StringArray* arr) {
    if (arr == NULL || arr->count == 0) return NULL;
    arr->count--;
    return arr->data[arr->count];
}

static inline void StringArray_clear(StringArray* arr) {
    if (arr == NULL) return;
    if (arr->destructor != NULL) {
        for (size_t i = 0; i < arr->count; i++) {
            if (arr->data[i] != NULL) {
                arr->destructor(arr->data[i]);
            }
        }
    }
    arr->count = 0;
}

#endif /* PTRARRAY_H */
