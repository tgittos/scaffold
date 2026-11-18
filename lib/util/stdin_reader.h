/* Read all of a FILE stream into a heap-allocated string.
 *
 * Returns a NUL-terminated string the caller must free, or NULL on error.
 * If the input exceeds max_bytes, returns NULL and sets *truncated = 1.
 * On other errors (OOM, read failure) also returns NULL with *truncated = 0.
 */
#ifndef LIB_UTIL_STDIN_READER_H
#define LIB_UTIL_STDIN_READER_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read all bytes from `in` up to `max_bytes`.
 *
 * @param in         FILE* to read (typically stdin).
 * @param max_bytes  Hard limit in bytes (8 MiB recommended).
 * @param truncated  Set to 1 if max_bytes was exceeded, 0 otherwise.
 * @return           Heap-allocated NUL-terminated string, or NULL on error.
 *                   Caller must free().
 */
char *stdin_read_all(FILE *in, size_t max_bytes, int *truncated);

#ifdef __cplusplus
}
#endif

#endif /* LIB_UTIL_STDIN_READER_H */
