#include "stdin_reader.h"
#include <stdlib.h>
#include <string.h>

#define CHUNK_SIZE 4096

char *stdin_read_all(FILE *in, size_t max_bytes, int *truncated) {
    if (in == NULL || truncated == NULL) return NULL;

    *truncated = 0;

    size_t capacity = CHUNK_SIZE;
    size_t length = 0;
    char *buf = malloc(capacity);
    if (buf == NULL) return NULL;

    while (1) {
        /* Ensure there's room for at least one chunk */
        if (length + CHUNK_SIZE > capacity) {
            size_t new_cap = capacity * 2;
            char *tmp = realloc(buf, new_cap);
            if (tmp == NULL) {
                free(buf);
                return NULL;
            }
            buf = tmp;
            capacity = new_cap;
        }

        size_t to_read = capacity - length;
        size_t n = fread(buf + length, 1, to_read, in);
        length += n;

        if (length > max_bytes) {
            free(buf);
            *truncated = 1;
            return NULL;
        }

        if (n < to_read) {
            /* Short read: check for I/O error vs. EOF */
            if (ferror(in)) {
                free(buf);
                return NULL; /* truncated stays 0 — distinguish from over-limit */
            }
            break; /* EOF */
        }
    }

    /* NUL-terminate */
    char *tmp = realloc(buf, length + 1);
    if (tmp == NULL) {
        free(buf);
        return NULL;
    }
    buf = tmp;
    buf[length] = '\0';
    return buf;
}
