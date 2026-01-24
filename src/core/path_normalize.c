/**
 * Cross-Platform Path Normalization Implementation
 *
 * Provides consistent path normalization for protected file detection
 * and allowlist pattern matching across Windows and POSIX platforms.
 */

#include "path_normalize.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/**
 * Normalize a filesystem path for cross-platform comparison.
 */
NormalizedPath *normalize_path(const char *path) {
    if (!path || !*path) {
        return NULL;
    }

    NormalizedPath *np = calloc(1, sizeof(*np));
    if (!np) {
        return NULL;
    }

    /* Start with a copy we can modify */
    char *work = strdup(path);
    if (!work) {
        free(np);
        return NULL;
    }

#ifdef _WIN32
    /* Windows: Convert backslashes to forward slashes */
    for (char *p = work; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }

    /* Windows: Lowercase the entire path (case-insensitive filesystem) */
    for (char *p = work; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }

    /* Windows: Handle drive letters - C:/foo -> /c/foo */
    if (isalpha((unsigned char)work[0]) && work[1] == ':') {
        size_t old_len = strlen(work);
        /* Need space for: / + drive_letter + rest_of_path + null */
        /* Original: C:/foo (len=6), becomes /c/foo (len=6) - same size */
        /* Original: C:foo (len=5), becomes /c/foo (len=6) - need +1 */
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
        new_work[1] = (char)tolower((unsigned char)work[0]);
        if (needs_slash) {
            new_work[2] = '/';
            strcpy(new_work + 3, work + 2);
        } else {
            strcpy(new_work + 2, work + 2);
        }

        free(work);
        work = new_work;
    }

    /* Windows: Handle UNC paths - //server/share -> /unc/server/share */
    if (work[0] == '/' && work[1] == '/') {
        size_t old_len = strlen(work);
        /* //server/share becomes /unc/server/share */
        /* Need to insert "unc" after first slash, removing one slash */
        char *new_work = malloc(old_len + 4); /* +4 for "unc" and null */
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
    /* POSIX: paths are case-sensitive, already use forward slashes */
    np->is_absolute = (work[0] == '/');
#endif

    /* Both platforms: Remove trailing slashes (except for root "/") */
    size_t len = strlen(work);
    while (len > 1 && work[len - 1] == '/') {
        work[--len] = '\0';
    }

    /* Both platforms: Collapse duplicate slashes */
    char *dst = work;
    char *src = work;
    while (*src) {
        if (*src == '/' && *(src + 1) == '/') {
            /* Skip duplicate slash */
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';

    np->normalized = work;

    /* Extract basename (final component) */
    char *last_slash = strrchr(np->normalized, '/');
    if (last_slash) {
        np->basename = last_slash + 1;
    } else {
        np->basename = np->normalized;
    }

    return np;
}

/**
 * Free a NormalizedPath structure.
 */
void free_normalized_path(NormalizedPath *np) {
    if (np) {
        free(np->normalized);
        /* basename points into normalized, don't free separately */
        free(np);
    }
}

/**
 * Compare two basenames using platform-appropriate case sensitivity.
 */
int path_basename_cmp(const char *a, const char *b) {
    if (!a || !b) {
        if (a == b) {
            return 0;
        }
        return a ? 1 : -1;
    }

#ifdef _WIN32
    /* Windows: case-insensitive comparison */
    return _stricmp(a, b);
#else
    /* POSIX: case-sensitive comparison */
    return strcmp(a, b);
#endif
}

/**
 * Check if a basename starts with a prefix using platform-appropriate
 * case sensitivity.
 */
int path_basename_has_prefix(const char *basename, const char *prefix) {
    if (!basename || !prefix) {
        return 0;
    }

    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) {
        return 1; /* Empty prefix matches everything */
    }

#ifdef _WIN32
    /* Windows: case-insensitive comparison */
    return _strnicmp(basename, prefix, prefix_len) == 0;
#else
    /* POSIX: case-sensitive comparison */
    return strncmp(basename, prefix, prefix_len) == 0;
#endif
}
