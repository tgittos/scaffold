#include "project_scope.h"
#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BASENAME_MAX_CHARS 32

static int has_git_entry(const char* dir) {
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/.git", dir);
    if (n < 0 || (size_t)n >= sizeof(path)) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

/* Truncates root in place to the project root. */
static void find_project_root(char* root) {
    char probe[PATH_MAX];
    snprintf(probe, sizeof(probe), "%s", root);

    for (;;) {
        if (has_git_entry(probe)) {
            snprintf(root, PATH_MAX, "%s", probe);
            return;
        }
        char* slash = strrchr(probe, '/');
        if (slash == NULL || slash == probe) return;
        *slash = '\0';
    }
}

static uint64_t fnv1a64(const char* s) {
    uint64_t h = 14695981039346656037ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Index names become file and directory names, so only [A-Za-z0-9_-]. */
static void sanitized_basename(const char* root, char* out) {
    const char* base = strrchr(root, '/');
    base = (base != NULL && base[1] != '\0') ? base + 1 : "root";

    size_t i = 0;
    for (; base[i] != '\0' && i < BASENAME_MAX_CHARS; i++) {
        unsigned char c = (unsigned char)base[i];
        out[i] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    }
    out[i] = '\0';
}

int project_scope_index_name_for(const char* dir, const char* prefix,
                                 char* out, size_t out_size) {
    if (dir == NULL || prefix == NULL || out == NULL) return -1;

    char root[PATH_MAX];
    if (realpath(dir, root) == NULL) return -1;
    find_project_root(root);

    char base[BASENAME_MAX_CHARS + 1];
    sanitized_basename(root, base);

    int n = snprintf(out, out_size, "%s_%s_%016llx",
                     prefix, base, (unsigned long long)fnv1a64(root));
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

int project_scope_index_name(const char* prefix, char* out, size_t out_size) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return -1;
    return project_scope_index_name_for(cwd, prefix, out, out_size);
}
