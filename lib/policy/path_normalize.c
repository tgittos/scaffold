#include "path_normalize.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

NormalizedPath *normalize_path(const char *path) {
    if (!path || !*path) {
        return NULL;
    }

    NormalizedPath *np = calloc(1, sizeof(*np));
    if (!np) {
        return NULL;
    }

    char *work = strdup(path);
    if (!work) {
        free(np);
        return NULL;
    }

#ifdef _WIN32
    for (char *p = work; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    for (char *p = work; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }

    /* Normalize drive letters to POSIX-style: C:/foo -> /c/foo */
    if (isalpha((unsigned char)work[0]) && work[1] == ':') {
        size_t old_len = strlen(work);
        size_t new_len = old_len;
        int needs_slash = (work[2] != '/');
        if (needs_slash) {
            new_len++;
        }

        char *new_work = malloc(new_len + 1);
        if (!new_work) {
            free(work);
            free(np);
            return NULL;
        }

        new_work[0] = '/';
        new_work[1] = work[0]; /* Already lowercased above */
        if (needs_slash) {
            new_work[2] = '/';
            strcpy(new_work + 3, work + 2);
        } else {
            strcpy(new_work + 2, work + 2);
        }

        free(work);
        work = new_work;
    }

    /* Normalize UNC paths: //server/share -> /unc/server/share */
    if (work[0] == '/' && work[1] == '/') {
        size_t old_len = strlen(work);
        char *new_work = malloc(old_len + 3 + 1);
        if (!new_work) {
            free(work);
            free(np);
            return NULL;
        }

        strcpy(new_work, "/unc");
        strcat(new_work, work + 1); /* Skip first slash of // */

        free(work);
        work = new_work;
    }

    np->is_absolute = (work[0] == '/');
#else
    np->is_absolute = (work[0] == '/');
#endif

    size_t len = strlen(work);
    while (len > 1 && work[len - 1] == '/') {
        work[--len] = '\0';
    }

    char *dst = work;
    char *src = work;
    while (*src) {
        if (*src == '/' && *(src + 1) == '/') {
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    np->normalized = work;

    char *last_slash = strrchr(np->normalized, '/');
    if (last_slash) {
        np->basename = last_slash + 1;
    } else {
        np->basename = np->normalized;
    }

    return np;
}

void free_normalized_path(NormalizedPath *np) {
    if (np) {
        free(np->normalized);
        /* basename points into normalized, don't free separately */
        free(np);
    }
}

int path_basename_cmp(const char *a, const char *b) {
    if (!a || !b) {
        if (a == b) {
            return 0;
        }
        return a ? 1 : -1;
    }

#ifdef _WIN32
    return _stricmp(a, b);
#else
    return strcmp(a, b);
#endif
}

int path_basename_has_prefix(const char *basename, const char *prefix) {
    if (!basename || !prefix) {
        return 0;
    }

    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) {
        return 1; /* Empty prefix matches everything */
    }

#ifdef _WIN32
    return _strnicmp(basename, prefix, prefix_len) == 0;
#else
    return strncmp(basename, prefix, prefix_len) == 0;
#endif
}
