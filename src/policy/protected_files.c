/**
 * Protected Files Detection Implementation
 *
 * Detects and blocks modification of protected configuration files:
 * - ralph.config.json (and any path ending in /ralph.config.json)
 * - .ralph/config.json
 * - .env files (.env, .env.local, .env.production, etc.)
 *
 * This protection is enforced at the tool execution layer and cannot be
 * bypassed by gate configuration or allowlist settings.
 *
 * Detection strategies:
 * 1. Basename exact match (e.g., "ralph.config.json", ".env")
 * 2. Basename prefix match (e.g., ".env.*")
 * 3. Glob pattern match (e.g., any path ending in /.ralph/config.json)
 * 4. Inode-based detection (catches hardlinks and renames)
 */

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

/* ============================================================================
 * Internal Constants
 * ========================================================================== */

/* Maximum path buffer size for internal operations */
#define PROTECTED_PATH_BUFSIZE 4096

/* ============================================================================
 * Protected File Patterns
 * ========================================================================== */

/**
 * Exact basename patterns that are always protected.
 * These filenames are blocked regardless of directory location.
 */
static const char *PROTECTED_BASENAME_PATTERNS[] = {
    "ralph.config.json",
    ".env",
    NULL
};

/**
 * Basename prefix patterns.
 * Any file whose basename starts with these prefixes is protected.
 * This catches .env.local, .env.production, .env.development, etc.
 */
static const char *PROTECTED_PREFIX_PATTERNS[] = {
    ".env.",
    NULL
};

/**
 * Glob patterns for full path matching.
 * These patterns use fnmatch() with FNM_PATHNAME for proper matching.
 * All patterns use forward slashes after normalization.
 */
static const char *PROTECTED_GLOB_PATTERNS[] = {
    "**/ralph.config.json",
    "**/.ralph/config.json",
    "**/.env",
    "**/.env.*",
    NULL
};

/* ============================================================================
 * Global Inode Cache
 * ========================================================================== */

static ProtectedInodeCache inode_cache = {
    .inodes = NULL,
    .count = 0,
    .capacity = 0,
    .last_refresh = 0
};

static int module_initialized = 0;

/* ============================================================================
 * Internal Helper Functions
 * ========================================================================== */

/**
 * Ensure the inode cache has capacity for at least one more entry.
 * Doubles capacity when needed.
 *
 * @return 0 on success, -1 on allocation failure
 */
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

/**
 * Check if an inode is already in the cache.
 *
 * @param device Device ID
 * @param inode Inode number
 * @return 1 if found, 0 if not
 */
static int inode_in_cache(dev_t device, ino_t inode) {
    for (int i = 0; i < inode_cache.count; i++) {
        if (inode_cache.inodes[i].device == device &&
            inode_cache.inodes[i].inode == inode) {
            return 1;
        }
    }
    return 0;
}

/**
 * Build a full path from directory and filename.
 *
 * @param dir Directory path
 * @param filename Filename to append
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return 0 on success, -1 if buffer too small
 */
static int build_path(const char *dir, const char *filename, char *buffer, size_t buffer_size) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(filename);

    /* Check if we need a separator */
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

/**
 * Scan paths for protected files relative to a base directory.
 *
 * @param base_dir Base directory to scan from
 */
static void scan_protected_paths_in_dir(const char *base_dir) {
    /* Standard protected file locations relative to a directory */
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
    memset(full_path, 0, sizeof(full_path)); /* Initialize to silence valgrind */

    for (int i = 0; SCAN_FILENAMES[i]; i++) {
        if (build_path(base_dir, SCAN_FILENAMES[i], full_path, sizeof(full_path)) == 0) {
            add_protected_inode_if_exists(full_path);
        }
    }
}

/* ============================================================================
 * Inode Cache Management
 * ========================================================================== */

void refresh_protected_inodes(void) {
    time_t now = time(NULL);
    if (now - inode_cache.last_refresh < PROTECTED_INODE_REFRESH_INTERVAL) {
        return; /* Cache is still fresh */
    }

    /* Clear existing entries */
    clear_protected_inode_cache();

    /* Get current working directory */
    char cwd[PROTECTED_PATH_BUFSIZE];
    if (!getcwd(cwd, sizeof(cwd))) {
        /* Can't get cwd, just update timestamp and return */
        inode_cache.last_refresh = now;
        return;
    }

    /* Scan current directory */
    scan_protected_paths_in_dir(cwd);

    /* Scan parent directories up to PROTECTED_INODE_SCAN_DEPTH levels */
    char parent_dir[PROTECTED_PATH_BUFSIZE];
    strncpy(parent_dir, cwd, sizeof(parent_dir) - 1);
    parent_dir[sizeof(parent_dir) - 1] = '\0';

    for (int depth = 0; depth < PROTECTED_INODE_SCAN_DEPTH; depth++) {
        /* Find and truncate at last slash */
        char *last_slash = strrchr(parent_dir, '/');
        if (!last_slash || last_slash == parent_dir) {
            /* Reached root or no more parent directories */
            break;
        }
        *last_slash = '\0';

        scan_protected_paths_in_dir(parent_dir);
    }

    /* Also scan root for protected files */
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
        return; /* File doesn't exist */
    }

    /* Check if already tracked by POSIX inode */
    if (inode_in_cache(st.st_dev, st.st_ino)) {
        return;
    }

    /* Ensure capacity */
    if (ensure_cache_capacity() != 0) {
        return; /* Allocation failed */
    }

    /* Add to cache */
    ProtectedInode *pi = &inode_cache.inodes[inode_cache.count];
    pi->device = st.st_dev;
    pi->inode = st.st_ino;
    pi->original_path = strdup(path);
    pi->discovered_at = time(NULL);

    if (!pi->original_path) {
        return; /* strdup failed, don't increment count */
    }

#ifdef _WIN32
    /* Get Windows file identity */
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
    /* Keep allocated capacity for reuse */
}

void cleanup_protected_inode_cache(void) {
    clear_protected_inode_cache();
    free(inode_cache.inodes);
    inode_cache.inodes = NULL;
    inode_cache.capacity = 0;
    inode_cache.last_refresh = 0;
}

/* ============================================================================
 * Protected Patterns Access
 * ========================================================================== */

const char **get_protected_basename_patterns(void) {
    return PROTECTED_BASENAME_PATTERNS;
}

const char **get_protected_prefix_patterns(void) {
    return PROTECTED_PREFIX_PATTERNS;
}

const char **get_protected_glob_patterns(void) {
    return PROTECTED_GLOB_PATTERNS;
}

/* ============================================================================
 * Core Detection Functions
 * ========================================================================== */

int is_protected_basename(const char *basename) {
    if (!basename || !*basename) {
        return 0;
    }

    /* Check exact basename patterns */
    for (int i = 0; PROTECTED_BASENAME_PATTERNS[i]; i++) {
        if (path_basename_cmp(basename, PROTECTED_BASENAME_PATTERNS[i]) == 0) {
            return 1;
        }
    }

    /* Check prefix patterns */
    for (int i = 0; PROTECTED_PREFIX_PATTERNS[i]; i++) {
        if (path_basename_has_prefix(basename, PROTECTED_PREFIX_PATTERNS[i])) {
            return 1;
        }
    }

    return 0;
}

/**
 * Check if a pattern suffix matches a path component.
 * Handles wildcards in the suffix using fnmatch.
 *
 * @param path_component The path component to check (e.g., "file.txt" or ".env.local")
 * @param suffix_pattern The suffix pattern to match (may contain wildcards)
 * @return 1 if matches, 0 otherwise
 */
static int suffix_matches(const char *path_component, const char *suffix_pattern) {
    /* Check for wildcards in the pattern */
    int has_wildcard = (strchr(suffix_pattern, '*') != NULL ||
                        strchr(suffix_pattern, '?') != NULL ||
                        strchr(suffix_pattern, '[') != NULL);

    if (has_wildcard) {
        /* Use fnmatch for wildcard patterns */
        int flags = 0;
#ifdef _WIN32
        /* Windows is case-insensitive, but FNM_CASEFOLD may not be available */
        /* Path is already lowercased by normalize_path, patterns are lowercase */
#endif
        return fnmatch(suffix_pattern, path_component, flags) == 0;
    } else {
        /* Literal comparison */
#ifdef _WIN32
        return _stricmp(path_component, suffix_pattern) == 0;
#else
        return strcmp(path_component, suffix_pattern) == 0;
#endif
    }
}

/**
 * Check if a path matches a ** glob pattern.
 * For patterns like "**\/foo.txt", checks if path ends with "/foo.txt"
 * or the path basename equals "foo.txt".
 *
 * @param path The path to check
 * @param suffix The suffix after **\/ (may contain wildcards)
 * @return 1 if matches, 0 otherwise
 */
static int path_matches_recursive_pattern(const char *path, const char *suffix) {
    size_t path_len = strlen(path);

    /* Check if suffix contains a directory component */
    const char *suffix_slash = strchr(suffix, '/');

    if (suffix_slash) {
        /* Suffix has directory structure like ".ralph/config.json" */
        /* Check if path ends with /suffix or equals suffix */
        size_t suffix_len = strlen(suffix);

        if (suffix_len > path_len) {
            return 0;
        }

        /* Check exact match (path == suffix) */
        if (path_len == suffix_len) {
            return suffix_matches(path, suffix);
        }

        /* Check if path ends with /suffix */
        const char *path_suffix = path + path_len - suffix_len;
        char preceding = *(path_suffix - 1);
        if (preceding == '/') {
            return suffix_matches(path_suffix, suffix);
        }

        return 0;
    } else {
        /* Suffix is just a filename pattern like ".env" or ".env.*" */
        /* Extract basename from path and compare */
        const char *basename = strrchr(path, '/');
        basename = basename ? basename + 1 : path;

        return suffix_matches(basename, suffix);
    }
}

int matches_protected_glob(const char *path) {
    if (!path || !*path) {
        return 0;
    }

    /*
     * Custom glob matching for recursive patterns.
     * Standard fnmatch doesn't support double-star for recursive directory matching.
     *
     * For patterns like "star-star/foo.txt", we check if path ends with "/foo.txt"
     * or the basename matches "foo.txt".
     */
    for (int i = 0; PROTECTED_GLOB_PATTERNS[i]; i++) {
        const char *pattern = PROTECTED_GLOB_PATTERNS[i];

        /* Handle recursive patterns (starting with star-star-slash) */
        if (pattern[0] == '*' && pattern[1] == '*' && pattern[2] == '/') {
            /* Pattern is recursive - check if path matches the suffix */
            const char *suffix = pattern + 3; /* Skip the first 3 chars */
            if (path_matches_recursive_pattern(path, suffix)) {
                return 1;
            }
        } else {
            /* Use standard fnmatch for other patterns */
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
        return 0; /* Can't stat, not protected by inode */
    }

    /* Check POSIX inode match */
    for (int i = 0; i < inode_cache.count; i++) {
        if (st.st_dev == inode_cache.inodes[i].device &&
            st.st_ino == inode_cache.inodes[i].inode) {
            return 1;
        }
    }

#ifdef _WIN32
    /* Also check by Windows file index */
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

    /* Ensure module is initialized */
    if (!module_initialized) {
        protected_files_init();
    }

    /* Refresh inode cache if stale */
    refresh_protected_inodes();

    /* Normalize the path for consistent matching */
    NormalizedPath *np = normalize_path(path);
    if (!np) {
        /*
         * If normalization fails, fall back to checking the raw path.
         * This is conservative - we'd rather over-protect than under-protect.
         */

        /* Check raw basename (find last component) */
        const char *basename = strrchr(path, '/');
#ifdef _WIN32
        /* Also check backslash on Windows */
        const char *bslash = strrchr(path, '\\');
        if (bslash && (!basename || bslash > basename)) {
            basename = bslash;
        }
#endif
        basename = basename ? basename + 1 : path;

        if (is_protected_basename(basename)) {
            return 1;
        }

        /* Can't do reliable glob or inode check without normalization */
        return 0;
    }

    int protected = 0;

    /* Strategy 1: Check basename against exact patterns */
    if (is_protected_basename(np->basename)) {
        protected = 1;
        goto cleanup;
    }

    /* Strategy 2: Check full path against glob patterns */
    if (matches_protected_glob(np->normalized)) {
        protected = 1;
        goto cleanup;
    }

    /* Strategy 3: Check by inode (catches hardlinks and renames) */
    if (is_protected_inode(path)) {
        protected = 1;
        goto cleanup;
    }

cleanup:
    free_normalized_path(np);
    return protected;
}

/* ============================================================================
 * Initialization and Cleanup
 * ========================================================================== */

int protected_files_init(void) {
    if (module_initialized) {
        return 0;
    }

    /* Initial inode cache setup is done lazily on first refresh */
    module_initialized = 1;

    /* Force initial scan */
    force_protected_inode_refresh();

    return 0;
}

void protected_files_cleanup(void) {
    cleanup_protected_inode_cache();
    module_initialized = 0;
}
