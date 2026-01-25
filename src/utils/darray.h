/**
 * darray.h - Type-safe dynamic array implementation
 *
 * A macro-based dynamic array that generates type-specific functions.
 * Eliminates boilerplate while maintaining type safety and the existing
 * patterns used throughout the codebase.
 *
 * Usage:
 *   // In header: declare the array type and function prototypes
 *   DARRAY_DECLARE(IntArray, int)
 *
 *   // In source: define the function implementations
 *   DARRAY_DEFINE(IntArray, int)
 *
 *   // Or for header-only use in a single compilation unit:
 *   DARRAY_IMPL(IntArray, int)
 *
 * This generates:
 *   - IntArray (typedef struct with data, count, capacity)
 *   - intarray_init(arr) / intarray_init_capacity(arr, cap)
 *   - intarray_destroy(arr)
 *   - intarray_push(arr, item)
 *   - intarray_pop(arr, out_item)
 *   - intarray_get(arr, index)
 *   - intarray_set(arr, index, item)
 *   - intarray_insert(arr, index, item)
 *   - intarray_remove(arr, index)
 *   - intarray_clear(arr)
 *   - intarray_shrink(arr)
 *   - intarray_reserve(arr, capacity)
 */

#ifndef DARRAY_H
#define DARRAY_H

#include <stdlib.h>
#include <string.h>

/* Default initial capacity when none specified */
#define DARRAY_DEFAULT_CAPACITY 8

/* Growth factor (2x is standard and cache-friendly) */
#define DARRAY_GROWTH_FACTOR 2

/**
 * Helper macro to generate lowercase function prefix from type name
 * Note: This creates a simple lowercase version - callers should ensure
 * type names are simple identifiers (e.g., IntArray, TodoList)
 */
#define DARRAY_PASTE(a, b) a##b
#define DARRAY_PASTE2(a, b) DARRAY_PASTE(a, b)

/**
 * DARRAY_DECLARE - Declare a dynamic array type and its function prototypes
 *
 * @param NAME  The name of the array type (e.g., IntArray)
 * @param TYPE  The element type (e.g., int, Todo, char*)
 *
 * Generates a struct typedef and function declarations.
 * Use in header files.
 */
#define DARRAY_DECLARE(NAME, TYPE)                                            \
    typedef struct NAME {                                                     \
        TYPE* data;                                                           \
        size_t count;                                                         \
        size_t capacity;                                                      \
    } NAME;                                                                   \
                                                                              \
    int NAME##_init(NAME* arr);                                               \
    int NAME##_init_capacity(NAME* arr, size_t initial_capacity);             \
    void NAME##_destroy(NAME* arr);                                           \
    int NAME##_push(NAME* arr, TYPE item);                                    \
    int NAME##_pop(NAME* arr, TYPE* out_item);                                \
    TYPE* NAME##_get(NAME* arr, size_t index);                                \
    int NAME##_set(NAME* arr, size_t index, TYPE item);                       \
    int NAME##_insert(NAME* arr, size_t index, TYPE item);                    \
    int NAME##_remove(NAME* arr, size_t index);                               \
    void NAME##_clear(NAME* arr);                                             \
    int NAME##_shrink(NAME* arr);                                             \
    int NAME##_reserve(NAME* arr, size_t min_capacity);

/**
 * DARRAY_DEFINE - Define the function implementations for a dynamic array
 *
 * @param NAME  The name of the array type (must match DARRAY_DECLARE)
 * @param TYPE  The element type (must match DARRAY_DECLARE)
 *
 * Generates function definitions. Use in source files.
 */
#define DARRAY_DEFINE(NAME, TYPE)                                             \
                                                                              \
int NAME##_init(NAME* arr) {                                                  \
    return NAME##_init_capacity(arr, DARRAY_DEFAULT_CAPACITY);                \
}                                                                             \
                                                                              \
int NAME##_init_capacity(NAME* arr, size_t initial_capacity) {                \
    if (arr == NULL) return -1;                                               \
    if (initial_capacity == 0) initial_capacity = DARRAY_DEFAULT_CAPACITY;    \
    arr->data = malloc(sizeof(TYPE) * initial_capacity);                      \
    if (arr->data == NULL) return -1;                                         \
    arr->count = 0;                                                           \
    arr->capacity = initial_capacity;                                         \
    return 0;                                                                 \
}                                                                             \
                                                                              \
void NAME##_destroy(NAME* arr) {                                              \
    if (arr == NULL) return;                                                  \
    free(arr->data);                                                          \
    arr->data = NULL;                                                         \
    arr->count = 0;                                                           \
    arr->capacity = 0;                                                        \
}                                                                             \
                                                                              \
static int NAME##_grow(NAME* arr) {                                           \
    if (arr == NULL) return -1;                                               \
    size_t new_capacity = arr->capacity * DARRAY_GROWTH_FACTOR;               \
    if (new_capacity == 0) new_capacity = DARRAY_DEFAULT_CAPACITY;            \
    TYPE* new_data = realloc(arr->data, sizeof(TYPE) * new_capacity);         \
    if (new_data == NULL) return -1;                                          \
    arr->data = new_data;                                                     \
    arr->capacity = new_capacity;                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_push(NAME* arr, TYPE item) {                                       \
    if (arr == NULL) return -1;                                               \
    if (arr->count >= arr->capacity) {                                        \
        if (NAME##_grow(arr) != 0) return -1;                                 \
    }                                                                         \
    arr->data[arr->count] = item;                                             \
    arr->count++;                                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_pop(NAME* arr, TYPE* out_item) {                                   \
    if (arr == NULL || arr->count == 0) return -1;                            \
    arr->count--;                                                             \
    if (out_item != NULL) {                                                   \
        *out_item = arr->data[arr->count];                                    \
    }                                                                         \
    return 0;                                                                 \
}                                                                             \
                                                                              \
TYPE* NAME##_get(NAME* arr, size_t index) {                                   \
    if (arr == NULL || index >= arr->count) return NULL;                      \
    return &arr->data[index];                                                 \
}                                                                             \
                                                                              \
int NAME##_set(NAME* arr, size_t index, TYPE item) {                          \
    if (arr == NULL || index >= arr->count) return -1;                        \
    arr->data[index] = item;                                                  \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_insert(NAME* arr, size_t index, TYPE item) {                       \
    if (arr == NULL || index > arr->count) return -1;                         \
    if (arr->count >= arr->capacity) {                                        \
        if (NAME##_grow(arr) != 0) return -1;                                 \
    }                                                                         \
    if (index < arr->count) {                                                 \
        memmove(&arr->data[index + 1], &arr->data[index],                     \
                (arr->count - index) * sizeof(TYPE));                         \
    }                                                                         \
    arr->data[index] = item;                                                  \
    arr->count++;                                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_remove(NAME* arr, size_t index) {                                  \
    if (arr == NULL || index >= arr->count) return -1;                        \
    if (index < arr->count - 1) {                                             \
        memmove(&arr->data[index], &arr->data[index + 1],                     \
                (arr->count - index - 1) * sizeof(TYPE));                     \
    }                                                                         \
    arr->count--;                                                             \
    return 0;                                                                 \
}                                                                             \
                                                                              \
void NAME##_clear(NAME* arr) {                                                \
    if (arr == NULL) return;                                                  \
    arr->count = 0;                                                           \
}                                                                             \
                                                                              \
int NAME##_shrink(NAME* arr) {                                                \
    if (arr == NULL || arr->count == 0) return -1;                            \
    if (arr->count == arr->capacity) return 0;                                \
    TYPE* new_data = realloc(arr->data, sizeof(TYPE) * arr->count);           \
    if (new_data == NULL) return -1;                                          \
    arr->data = new_data;                                                     \
    arr->capacity = arr->count;                                               \
    return 0;                                                                 \
}                                                                             \
                                                                              \
int NAME##_reserve(NAME* arr, size_t min_capacity) {                          \
    if (arr == NULL) return -1;                                               \
    if (min_capacity <= arr->capacity) return 0;                              \
    TYPE* new_data = realloc(arr->data, sizeof(TYPE) * min_capacity);         \
    if (new_data == NULL) return -1;                                          \
    arr->data = new_data;                                                     \
    arr->capacity = min_capacity;                                             \
    return 0;                                                                 \
}

/**
 * DARRAY_IMPL - Convenience macro for header-only usage
 *
 * Combines DARRAY_DECLARE and DARRAY_DEFINE for use in a single
 * compilation unit. Add 'static' before if only used in one file.
 */
#define DARRAY_IMPL(NAME, TYPE) \
    DARRAY_DECLARE(NAME, TYPE)  \
    DARRAY_DEFINE(NAME, TYPE)

#endif /* DARRAY_H */
