#include "protected_files.h"
#include "path_normalize.h"

#include <errno.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* Maximum path buffer size for internal operations */
#define PROTECTED_PATH_BUFSIZE 4096

static const char *PROTECTED_BASENAME_PATTERNS[] = {
    "ralph.config.json",
    ".env",
    NULL
};

static const char *PROTECTED_PREFIX_PATTERNS[] = {
    ".env.",
    NULL
};

static const char *PROTECTED_GLOB_PATTERNS[] = {
    "**/ralph.config.json",
    "**/.ralph/config.json",
    "**/.env",
    "**/.env.*",
    NULL
};

static ProtectedInodeCache inode_cache = {
    .inodes = NULL,
    .count = 0,
    .capacity = 0,
    .last_refresh = 0
};

static int module_initialized = 0;

static int ensure_cache_capacity(void) {
    if (inode_cache.count < inode_cache.capacity) {
        return 0;
    }

    int new_capacity = inode_cache.capacity ? inode_cache.capacity * 2
                                            : PROTECTED_INODE_INITIAL_CAPACITY;
    ProtectedInode *new_inodes = realloc(
        inode_cache.inodes,
        (size_t)new_capacity * sizeof(ProtectedInode)
    );
    if (!new_inodes) {
        return -1;
    }

    inode_cache.inodes = new_inodes;
    inode_cache.capacity = new_capacity;
    return 0;
}

static int inode_in_cache(dev_t device, ino_t inode) {
    for (int i = 0; i < inode_cache.count; i++) {
        if (inode_cache.inodes[i].device == device &&
            inode_cache.inodes[i].inode == inode) {
            return 1;
        }
    }
    return 0;
}

static int build_path(const char *dir, const char *filename, char *buffer, size_t buffer_size) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(filename);

    int need_sep = (dir_len > 0 && dir[dir_len - 1] != '/');
    size_t total = dir_len + (need_sep ? 1 : 0) + file_len + 1;

    if (total > buffer_size) {
        return -1;
    }

    strcpy(buffer, dir);
    if (need_sep) {
        strcat(buffer, "/");
    }
    strcat(buffer, filename);
    return 0;
}

static void scan_protected_paths_in_dir(const char *base_dir) {
    static const char *SCAN_FILENAMES[] = {
        "ralph.config.json",
        ".ralph/config.json",
        ".env",
        ".env.local",
        ".env.development",
        ".env.production",
        ".env.test",
        NULL
    };

    char full_path[PROTECTED_PATH_BUFSIZE];
    memset(full_path, 0, sizeof(full_path));

    for (int i = 0; SCAN_FILENAMES[i]; i++) {
        if (build_path(base_dir, SCAN_FILENAMES[i], full_path, sizeof(full_path)) == 0) {
            add_protected_inode_if_exists(full_path);
        }
    }
}

void refresh_protected_inodes(void) {
    time_t now = time(NULL);
    if (now - inode_cache.last_refresh < PROTECTED_INODE_REFRESH_INTERVAL) {
        return;
    }

    clear_protected_inode_cache();

    char cwd[PROTECTED_PATH_BUFSIZE];
    if (!getcwd(cwd, sizeof(cwd))) {
        inode_cache.last_refresh = now;
        return;
    }

    scan_protected_paths_in_dir(cwd);

    char parent_dir[PROTECTED_PATH_BUFSIZE];
    strncpy(parent_dir, cwd, sizeof(parent_dir) - 1);
    parent_dir[sizeof(parent_dir) - 1] = '\0';

    for (int depth = 0; depth < PROTECTED_INODE_SCAN_DEPTH; depth++) {
        char *last_slash = strrchr(parent_dir, '/');
        if (!last_slash || last_slash == parent_dir) {
            break;
        }
        *last_slash = '\0';

        scan_protected_paths_in_dir(parent_dir);
    }

    scan_protected_paths_in_dir("/");

    inode_cache.last_refresh = now;
}

void force_protected_inode_refresh(void) {
    inode_cache.last_refresh = 0;
    refresh_protected_inodes();
}

void add_protected_inode_if_exists(const char *path) {
    if (!path || !*path) {
        return;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }

    if (inode_in_cache(st.st_dev, st.st_ino)) {
        return;
    }

    if (ensure_cache_capacity() != 0) {
        return;
    }

    ProtectedInode *pi = &inode_cache.inodes[inode_cache.count];
    pi->device = st.st_dev;
    pi->inode = st.st_ino;
    pi->original_path = strdup(path);
    pi->discovered_at = time(NULL);

    if (!pi->original_path) {
        return;
    }

#ifdef _WIN32
    HANDLE h = CreateFileA(
        path,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileInformationByHandle(h, &info)) {
            pi->volume_serial = info.dwVolumeSerialNumber;
            pi->index_high = info.nFileIndexHigh;
            pi->index_low = info.nFileIndexLow;
        } else {
            pi->volume_serial = 0;
            pi->index_high = 0;
            pi->index_low = 0;
        }
        CloseHandle(h);
    } else {
        pi->volume_serial = 0;
        pi->index_high = 0;
        pi->index_low = 0;
    }
#endif

    inode_cache.count++;
}

void clear_protected_inode_cache(void) {
    for (int i = 0; i < inode_cache.count; i++) {
        free(inode_cache.inodes[i].original_path);
        inode_cache.inodes[i].original_path = NULL;
    }
    inode_cache.count = 0;
}

void cleanup_protected_inode_cache(void) {
    clear_protected_inode_cache();
    free(inode_cache.inodes);
    inode_cache.inodes = NULL;
    inode_cache.capacity = 0;
    inode_cache.last_refresh = 0;
}

const char **get_protected_basename_patterns(void) {
    return PROTECTED_BASENAME_PATTERNS;
}

const char **get_protected_prefix_patterns(void) {
    return PROTECTED_PREFIX_PATTERNS;
}

const char **get_protected_glob_patterns(void) {
    return PROTECTED_GLOB_PATTERNS;
}

int is_protected_basename(const char *basename) {
    if (!basename || !*basename) {
        return 0;
    }

    for (int i = 0; PROTECTED_BASENAME_PATTERNS[i]; i++) {
        if (path_basename_cmp(basename, PROTECTED_BASENAME_PATTERNS[i]) == 0) {
            return 1;
        }
    }

    for (int i = 0; PROTECTED_PREFIX_PATTERNS[i]; i++) {
        if (path_basename_has_prefix(basename, PROTECTED_PREFIX_PATTERNS[i])) {
            return 1;
        }
    }

    return 0;
}

static int suffix_matches(const char *path_component, const char *suffix_pattern) {
    int has_wildcard = (strchr(suffix_pattern, '*') != NULL ||
                        strchr(suffix_pattern, '?') != NULL ||
                        strchr(suffix_pattern, '[') != NULL);

    if (has_wildcard) {
        int flags = 0;
#ifdef _WIN32
        /* Path already lowercased by normalize_path; patterns are lowercase */
#endif
        return fnmatch(suffix_pattern, path_component, flags) == 0;
    } else {
#ifdef _WIN32
        return _stricmp(path_component, suffix_pattern) == 0;
#else
        return strcmp(path_component, suffix_pattern) == 0;
#endif
    }
}

static int path_matches_recursive_pattern(const char *path, const char *suffix) {
    size_t path_len = strlen(path);
    const char *suffix_slash = strchr(suffix, '/');

    if (suffix_slash) {
        size_t suffix_len = strlen(suffix);

        if (suffix_len > path_len) {
            return 0;
        }

        if (path_len == suffix_len) {
            return suffix_matches(path, suffix);
        }

        const char *path_suffix = path + path_len - suffix_len;
        char preceding = *(path_suffix - 1);
        if (preceding == '/') {
            return suffix_matches(path_suffix, suffix);
        }

        return 0;
    } else {
        const char *basename = strrchr(path, '/');
        basename = basename ? basename + 1 : path;

        return suffix_matches(basename, suffix);
    }
}

int matches_protected_glob(const char *path) {
    if (!path || !*path) {
        return 0;
    }

    /* fnmatch doesn't support ** for recursive directory matching */
    for (int i = 0; PROTECTED_GLOB_PATTERNS[i]; i++) {
        const char *pattern = PROTECTED_GLOB_PATTERNS[i];

        if (pattern[0] == '*' && pattern[1] == '*' && pattern[2] == '/') {
            const char *suffix = pattern + 3;
            if (path_matches_recursive_pattern(path, suffix)) {
                return 1;
            }
        } else {
            int flags = FNM_PATHNAME;
            if (fnmatch(pattern, path, flags) == 0) {
                return 1;
            }
        }
    }

    return 0;
}

int is_protected_inode(const char *path) {
    if (!path || !*path) {
        return 0;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }

    for (int i = 0; i < inode_cache.count; i++) {
        if (st.st_dev == inode_cache.inodes[i].device &&
            st.st_ino == inode_cache.inodes[i].inode) {
            return 1;
        }
    }

#ifdef _WIN32
    HANDLE h = CreateFileA(
        path,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (h != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileInformationByHandle(h, &info)) {
            for (int i = 0; i < inode_cache.count; i++) {
                if (info.dwVolumeSerialNumber == inode_cache.inodes[i].volume_serial &&
                    info.nFileIndexHigh == inode_cache.inodes[i].index_high &&
                    info.nFileIndexLow == inode_cache.inodes[i].index_low) {
                    CloseHandle(h);
                    return 1;
                }
            }
        }
        CloseHandle(h);
    }
#endif

    return 0;
}

int is_protected_file(const char *path) {
    if (!path || !*path) {
        return 0;
    }

    if (!module_initialized) {
        protected_files_init();
    }

    refresh_protected_inodes();

    NormalizedPath *np = normalize_path(path);
    if (!np) {
        /* Conservative fallback: check raw path to avoid under-protecting */
        const char *basename = strrchr(path, '/');
#ifdef _WIN32
        const char *bslash = strrchr(path, '\\');
        if (bslash && (!basename || bslash > basename)) {
            basename = bslash;
        }
#endif
        basename = basename ? basename + 1 : path;

        if (is_protected_basename(basename)) {
            return 1;
        }

        return 0;
    }

    int protected = 0;

    if (is_protected_basename(np->basename)) {
        protected = 1;
        goto cleanup;
    }

    if (matches_protected_glob(np->normalized)) {
        protected = 1;
        goto cleanup;
    }

    if (is_protected_inode(path)) {
        protected = 1;
        goto cleanup;
    }

cleanup:
    free_normalized_path(np);
    return protected;
}

int protected_files_init(void) {
    if (module_initialized) {
        return 0;
    }

    module_initialized = 1;
    force_protected_inode_refresh();

    return 0;
}

void protected_files_cleanup(void) {
    cleanup_protected_inode_cache();
    module_initialized = 0;
}
