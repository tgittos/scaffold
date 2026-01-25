/**
 * Atomic File Operations Implementation
 *
 * Provides TOCTOU-safe (time-of-check-to-time-of-use) file operations for
 * the approval gates system. When a user approves a file operation, this
 * module ensures the file hasn't changed between approval and execution.
 *
 * Implementation follows the spec in SPEC_APPROVAL_GATES.md:
 * - For existing files: Open with O_NOFOLLOW, verify inode matches approval
 * - For new files: Verify parent directory inode, create with O_EXCL
 * - Use openat() for atomic parent-relative operations
 * - Track file identity via inode/device (POSIX) or file index (Windows)
 */

#include "atomic_file.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__
#include <mntent.h>
#endif

#ifdef _WIN32
#include <io.h>
#include <shlwapi.h>
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

/* ============================================================================
 * Internal Constants
 * ========================================================================== */

/* Maximum path length for internal buffers */
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef __linux__
/* Network filesystem type strings for Linux /proc/mounts */
static const char *NETWORK_FS_TYPES[] = {
    "nfs",
    "nfs4",
    "cifs",
    "smbfs",
    "smb3",
    "afs",
    "fuse.sshfs",
    "fuse.rclone",
    NULL
};
#endif

/* ============================================================================
 * Error Message Utilities
 * ========================================================================== */

const char *verify_result_message(VerifyResult result) {
    switch (result) {
        case VERIFY_OK:
            return "Path verified successfully";
        case VERIFY_ERR_SYMLINK:
            return "Path is a symlink (not allowed for security)";
        case VERIFY_ERR_DELETED:
            return "File was deleted after approval";
        case VERIFY_ERR_OPEN:
            return "Failed to open file";
        case VERIFY_ERR_STAT:
            return "Failed to get file information";
        case VERIFY_ERR_INODE_MISMATCH:
            return "File changed since approval (inode mismatch)";
        case VERIFY_ERR_PARENT:
            return "Cannot access parent directory";
        case VERIFY_ERR_PARENT_CHANGED:
            return "Parent directory changed since approval";
        case VERIFY_ERR_ALREADY_EXISTS:
            return "File already exists";
        case VERIFY_ERR_CREATE:
            return "Failed to create file";
        case VERIFY_ERR_INVALID_PATH:
            return "Invalid or malformed path";
        case VERIFY_ERR_RESOLVE:
            return "Failed to resolve path";
        case VERIFY_ERR_NETWORK_FS:
            return "Network filesystem detected, verification unreliable";
        default:
            return "Unknown verification error";
    }
}

char *format_verify_error(VerifyResult result, const char *path) {
    const char *message = verify_result_message(result);
    const char *error_type;

    switch (result) {
        case VERIFY_ERR_SYMLINK:
            error_type = "symlink_rejected";
            break;
        case VERIFY_ERR_INODE_MISMATCH:
        case VERIFY_ERR_PARENT_CHANGED:
            error_type = "path_changed";
            break;
        case VERIFY_ERR_DELETED:
            error_type = "file_deleted";
            break;
        case VERIFY_ERR_ALREADY_EXISTS:
            error_type = "file_exists";
            break;
        case VERIFY_ERR_NETWORK_FS:
            error_type = "network_fs_warning";
            break;
        default:
            error_type = "verification_failed";
            break;
    }

    /* Allocate buffer for JSON output */
    size_t buf_size = strlen(message) + strlen(path) + strlen(error_type) + 128;
    char *buf = malloc(buf_size);
    if (!buf) {
        return NULL;
    }

    /* Escape path for JSON (simple escape of quotes and backslashes) */
    char escaped_path[PATH_MAX * 2];
    size_t j = 0;
    for (size_t i = 0; path[i] && j < sizeof(escaped_path) - 2; i++) {
        if (path[i] == '"' || path[i] == '\\') {
            escaped_path[j++] = '\\';
        }
        escaped_path[j++] = path[i];
    }
    escaped_path[j] = '\0';

    snprintf(buf, buf_size,
             "{\"error\": \"%s\", \"message\": \"%s\", \"path\": \"%s\"}",
             error_type, message, escaped_path);

    return buf;
}

/* ============================================================================
 * Path Utilities
 * ========================================================================== */

const char *atomic_file_basename(const char *path) {
    if (!path || !*path) {
        return ".";
    }

    /* Find last separator */
    const char *last_sep = strrchr(path, '/');
#ifdef _WIN32
    const char *last_bsep = strrchr(path, '\\');
    if (last_bsep && (!last_sep || last_bsep > last_sep)) {
        last_sep = last_bsep;
    }
#endif

    if (last_sep) {
        /* Handle trailing slashes */
        if (*(last_sep + 1) == '\0') {
            /* Path ends with separator, look for previous component */
            const char *end = last_sep;
            while (end > path && (*(end - 1) == '/' || *(end - 1) == '\\')) {
                end--;
            }
            if (end == path) {
                return path; /* Root or all separators */
            }
            /* Find separator before this component */
            const char *prev = end - 1;
            while (prev > path && *prev != '/' && *prev != '\\') {
                prev--;
            }
            if (*prev == '/' || *prev == '\\') {
                return prev + 1;
            }
            return path;
        }
        return last_sep + 1;
    }

    return path;
}

char *atomic_file_dirname(const char *path) {
    if (!path || !*path) {
        return strdup(".");
    }

    char *result = strdup(path);
    if (!result) {
        return NULL;
    }

    size_t len = strlen(result);

    /* Remove trailing slashes (but keep at least one char for root) */
    while (len > 1 && (result[len - 1] == '/' || result[len - 1] == '\\')) {
        result[--len] = '\0';
    }

    /* Find last separator */
    char *last_sep = strrchr(result, '/');
#ifdef _WIN32
    char *last_bsep = strrchr(result, '\\');
    if (last_bsep && (!last_sep || last_bsep > last_sep)) {
        last_sep = last_bsep;
    }
#endif

    if (last_sep) {
        if (last_sep == result) {
            /* Root directory */
            result[1] = '\0';
        } else {
            *last_sep = '\0';
        }
    } else {
        /* No separator found - relative path with just filename */
        free(result);
        return strdup(".");
    }

    return result;
}

char *atomic_file_resolve_path(const char *path, int must_exist) {
    if (!path || !*path) {
        return NULL;
    }

#ifdef _WIN32
    /* Windows: Use GetFullPathName */
    char resolved[MAX_PATH];
    DWORD len = GetFullPathNameA(path, MAX_PATH, resolved, NULL);
    if (len == 0 || len >= MAX_PATH) {
        if (!must_exist) {
            /* For new files, resolve parent and append basename */
            char *parent = atomic_file_dirname(path);
            if (!parent) return NULL;

            char parent_resolved[MAX_PATH];
            len = GetFullPathNameA(parent, MAX_PATH, parent_resolved, NULL);
            free(parent);
            if (len == 0 || len >= MAX_PATH) return NULL;

            const char *base = atomic_file_basename(path);
            size_t total = strlen(parent_resolved) + strlen(base) + 2;
            char *result = malloc(total);
            if (!result) return NULL;
            snprintf(result, total, "%s\\%s", parent_resolved, base);
            return result;
        }
        return NULL;
    }
    return strdup(resolved);
#else
    /* POSIX: Use realpath for existing files */
    if (must_exist) {
        char *resolved = realpath(path, NULL);
        return resolved; /* May be NULL if path doesn't exist */
    }

    /* For new files, resolve parent and append basename */
    char *parent = atomic_file_dirname(path);
    if (!parent) {
        return NULL;
    }

    char *parent_resolved = realpath(parent, NULL);
    free(parent);
    if (!parent_resolved) {
        return NULL;
    }

    const char *base = atomic_file_basename(path);
    size_t total = strlen(parent_resolved) + strlen(base) + 2;
    char *result = malloc(total);
    if (!result) {
        free(parent_resolved);
        return NULL;
    }

    snprintf(result, total, "%s/%s", parent_resolved, base);
    free(parent_resolved);
    return result;
#endif
}

/* ============================================================================
 * Network Filesystem Detection
 * ========================================================================== */

int is_network_filesystem(const char *path) {
    if (!path || !*path) {
        return 0;
    }

#ifdef __linux__
    /* Linux: Check /proc/mounts */
    char resolved[PATH_MAX];
    char *real_path = realpath(path, resolved);
    if (!real_path) {
        /* Path doesn't exist yet - check parent */
        char *parent = atomic_file_dirname(path);
        if (!parent) return 0;
        real_path = realpath(parent, resolved);
        free(parent);
        if (!real_path) return 0;
    }

    FILE *mounts = setmntent("/proc/mounts", "r");
    if (!mounts) {
        return 0;
    }

    struct mntent *entry;
    int is_network = 0;
    char best_match[PATH_MAX] = "";

    while ((entry = getmntent(mounts)) != NULL) {
        /* Check if this mount point is a prefix of our path */
        size_t mount_len = strlen(entry->mnt_dir);
        if (strncmp(resolved, entry->mnt_dir, mount_len) == 0) {
            /* Check it's a proper prefix (either exact match or followed by /) */
            if (resolved[mount_len] == '\0' || resolved[mount_len] == '/') {
                /* Keep track of longest (most specific) match */
                if (mount_len > strlen(best_match)) {
                    strncpy(best_match, entry->mnt_dir, sizeof(best_match) - 1);
                    best_match[sizeof(best_match) - 1] = '\0';

                    /* Check if this is a network filesystem type */
                    is_network = 0;
                    for (int i = 0; NETWORK_FS_TYPES[i]; i++) {
                        if (strcmp(entry->mnt_type, NETWORK_FS_TYPES[i]) == 0) {
                            is_network = 1;
                            break;
                        }
                    }
                }
            }
        }
    }

    endmntent(mounts);
    return is_network;

#elif defined(_WIN32)
    /* Windows: Check drive type */
    char root[4];
    if (path[0] && path[1] == ':') {
        root[0] = path[0];
        root[1] = ':';
        root[2] = '\\';
        root[3] = '\0';
    } else if (path[0] == '\\' && path[1] == '\\') {
        /* UNC path - always network */
        return 1;
    } else {
        /* Relative path - check current drive */
        DWORD len = GetCurrentDirectoryA(0, NULL);
        if (len == 0) return 0;
        char *cwd = malloc(len);
        if (!cwd) return 0;
        GetCurrentDirectoryA(len, cwd);
        if (cwd[1] == ':') {
            root[0] = cwd[0];
            root[1] = ':';
            root[2] = '\\';
            root[3] = '\0';
        }
        free(cwd);
    }

    UINT drive_type = GetDriveTypeA(root);
    return (drive_type == DRIVE_REMOTE);

#else
    /* Other POSIX: Conservative approach - check for common network paths */
    if (strncmp(path, "/net/", 5) == 0 ||
        strncmp(path, "/nfs/", 5) == 0 ||
        strncmp(path, "/mnt/", 5) == 0) {
        /* These are common network mount points, but we can't be sure */
        /* Return 0 and let the caller decide */
    }
    return 0;
#endif
}

/* ============================================================================
 * ApprovedPath Management
 * ========================================================================== */

void init_approved_path(ApprovedPath *ap) {
    if (!ap) return;
    memset(ap, 0, sizeof(*ap));
}

void free_approved_path(ApprovedPath *ap) {
    if (!ap) return;

    free(ap->user_path);
    free(ap->resolved_path);
    free(ap->parent_path);

    ap->user_path = NULL;
    ap->resolved_path = NULL;
    ap->parent_path = NULL;
}

VerifyResult capture_approved_path(const char *path, ApprovedPath *out) {
    if (!path || !*path || !out) {
        return VERIFY_ERR_INVALID_PATH;
    }

    init_approved_path(out);

    /* Store user-provided path */
    out->user_path = strdup(path);
    if (!out->user_path) {
        return VERIFY_ERR_RESOLVE;
    }

    /* Check if file exists */
    struct stat st;
    int exists = (stat(path, &st) == 0);

    if (exists) {
        /* Existing file: capture its identity */
        out->existed = 1;
        out->inode = st.st_ino;
        out->device = st.st_dev;

        /* Resolve to canonical path */
        out->resolved_path = atomic_file_resolve_path(path, 1);
        if (!out->resolved_path) {
            free_approved_path(out);
            return VERIFY_ERR_RESOLVE;
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
                out->volume_serial = info.dwVolumeSerialNumber;
                out->index_high = info.nFileIndexHigh;
                out->index_low = info.nFileIndexLow;
            }
            CloseHandle(h);
        }
#endif
    } else {
        /* New file: capture parent directory identity */
        out->existed = 0;

        /* Get parent directory */
        out->parent_path = atomic_file_dirname(path);
        if (!out->parent_path) {
            free_approved_path(out);
            return VERIFY_ERR_RESOLVE;
        }

        /* Stat parent directory */
        if (stat(out->parent_path, &st) != 0) {
            free_approved_path(out);
            return VERIFY_ERR_PARENT;
        }

        out->parent_inode = st.st_ino;
        out->parent_device = st.st_dev;

        /* Resolve path (parent + basename) */
        out->resolved_path = atomic_file_resolve_path(path, 0);
        if (!out->resolved_path) {
            free_approved_path(out);
            return VERIFY_ERR_RESOLVE;
        }

#ifdef _WIN32
        /* Get Windows parent directory identity */
        HANDLE h = CreateFileA(
            out->parent_path,
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
                out->parent_volume_serial = info.dwVolumeSerialNumber;
                out->parent_index_high = info.nFileIndexHigh;
                out->parent_index_low = info.nFileIndexLow;
            }
            CloseHandle(h);
        }
#endif
    }

    /* Check for network filesystem */
    out->is_network_fs = is_network_filesystem(path);

    return VERIFY_OK;
}

/* ============================================================================
 * Execution-Time Verification
 * ========================================================================== */

VerifyResult verify_approved_path(const ApprovedPath *approved) {
    if (!approved || !approved->resolved_path) {
        return VERIFY_ERR_INVALID_PATH;
    }

    if (approved->existed) {
        /* Verify existing file still has same identity */
        struct stat st;
        if (stat(approved->resolved_path, &st) != 0) {
            if (errno == ENOENT) {
                return VERIFY_ERR_DELETED;
            }
            return VERIFY_ERR_STAT;
        }

        /* Check inode and device match */
        if (st.st_ino != approved->inode || st.st_dev != approved->device) {
            return VERIFY_ERR_INODE_MISMATCH;
        }

#ifdef _WIN32
        /* Also verify Windows file identity */
        HANDLE h = CreateFileA(
            approved->resolved_path,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL
        );
        if (h == INVALID_HANDLE_VALUE) {
            return VERIFY_ERR_OPEN;
        }

        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(h, &info)) {
            CloseHandle(h);
            return VERIFY_ERR_STAT;
        }
        CloseHandle(h);

        if (info.dwVolumeSerialNumber != approved->volume_serial ||
            info.nFileIndexHigh != approved->index_high ||
            info.nFileIndexLow != approved->index_low) {
            return VERIFY_ERR_INODE_MISMATCH;
        }
#endif
    } else {
        /* Verify parent directory still has same identity */
        if (!approved->parent_path) {
            return VERIFY_ERR_INVALID_PATH;
        }

        struct stat st;
        if (stat(approved->parent_path, &st) != 0) {
            return VERIFY_ERR_PARENT;
        }

        if (st.st_ino != approved->parent_inode ||
            st.st_dev != approved->parent_device) {
            return VERIFY_ERR_PARENT_CHANGED;
        }

#ifdef _WIN32
        /* Also verify Windows parent identity */
        HANDLE h = CreateFileA(
            approved->parent_path,
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,
            NULL
        );
        if (h == INVALID_HANDLE_VALUE) {
            return VERIFY_ERR_PARENT;
        }

        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(h, &info)) {
            CloseHandle(h);
            return VERIFY_ERR_STAT;
        }
        CloseHandle(h);

        if (info.dwVolumeSerialNumber != approved->parent_volume_serial ||
            info.nFileIndexHigh != approved->parent_index_high ||
            info.nFileIndexLow != approved->parent_index_low) {
            return VERIFY_ERR_PARENT_CHANGED;
        }
#endif
    }

    return VERIFY_OK;
}

VerifyResult verify_and_open_approved_path(const ApprovedPath *approved,
                                           int flags,
                                           int *out_fd) {
    if (!approved || !approved->resolved_path || !out_fd) {
        return VERIFY_ERR_INVALID_PATH;
    }

    *out_fd = -1;

    if (approved->existed) {
        /* Existing file: open with O_NOFOLLOW and verify inode */
        /*
         * IMPORTANT: Open user_path with O_NOFOLLOW, not resolved_path.
         * resolved_path comes from realpath() which follows symlinks.
         * We need to detect if the original path has become a symlink
         * since approval time.
         */
        const char *path_to_open = approved->user_path ? approved->user_path
                                                       : approved->resolved_path;

#ifdef _WIN32
        /* Windows: Use CreateFile with FILE_FLAG_OPEN_REPARSE_POINT */
        DWORD access = 0;
        if (flags & O_RDONLY) access = GENERIC_READ;
        if (flags & O_WRONLY) access = GENERIC_WRITE;
        if (flags & O_RDWR) access = GENERIC_READ | GENERIC_WRITE;

        DWORD creation = OPEN_EXISTING;

        HANDLE h = CreateFileA(
            path_to_open,
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            creation,
            FILE_FLAG_OPEN_REPARSE_POINT,
            NULL
        );

        if (h == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND) {
                return VERIFY_ERR_DELETED;
            }
            return VERIFY_ERR_OPEN;
        }

        /* Check if it's a reparse point (symlink/junction) */
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(h, &info)) {
            CloseHandle(h);
            return VERIFY_ERR_STAT;
        }

        if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            CloseHandle(h);
            return VERIFY_ERR_SYMLINK;
        }

        /* Verify file identity */
        if (info.dwVolumeSerialNumber != approved->volume_serial ||
            info.nFileIndexHigh != approved->index_high ||
            info.nFileIndexLow != approved->index_low) {
            CloseHandle(h);
            return VERIFY_ERR_INODE_MISMATCH;
        }

        /* Convert HANDLE to file descriptor */
        *out_fd = _open_osfhandle((intptr_t)h, flags);
        if (*out_fd == -1) {
            CloseHandle(h);
            return VERIFY_ERR_OPEN;
        }
#else
        /* POSIX: Use O_NOFOLLOW to reject symlinks */
        int open_flags = flags | O_NOFOLLOW;
        int fd = open(path_to_open, open_flags);

        if (fd < 0) {
            if (errno == ELOOP || errno == EMLINK) {
                return VERIFY_ERR_SYMLINK;
            }
            if (errno == ENOENT) {
                return VERIFY_ERR_DELETED;
            }
            return VERIFY_ERR_OPEN;
        }

        /* Verify inode/device match */
        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            return VERIFY_ERR_STAT;
        }

        if (st.st_ino != approved->inode || st.st_dev != approved->device) {
            close(fd);
            return VERIFY_ERR_INODE_MISMATCH;
        }

        *out_fd = fd;
#endif
        return VERIFY_OK;

    } else {
        /* New file: verify parent and create with O_EXCL */
        return create_file_in_verified_parent(approved, flags, 0644, out_fd);
    }
}

/* ============================================================================
 * Parent Directory Verification
 * ========================================================================== */

VerifyResult open_verified_parent(const ApprovedPath *approved, int *out_fd) {
    if (!approved || !approved->parent_path || !out_fd) {
        return VERIFY_ERR_INVALID_PATH;
    }

    *out_fd = -1;

#ifdef _WIN32
    /* Windows: Open directory with backup semantics */
    HANDLE h = CreateFileA(
        approved->parent_path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );

    if (h == INVALID_HANDLE_VALUE) {
        return VERIFY_ERR_PARENT;
    }

    /* Verify directory identity */
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(h, &info)) {
        CloseHandle(h);
        return VERIFY_ERR_STAT;
    }

    if (info.dwVolumeSerialNumber != approved->parent_volume_serial ||
        info.nFileIndexHigh != approved->parent_index_high ||
        info.nFileIndexLow != approved->parent_index_low) {
        CloseHandle(h);
        return VERIFY_ERR_PARENT_CHANGED;
    }

    /* Convert HANDLE to fd */
    *out_fd = _open_osfhandle((intptr_t)h, O_RDONLY);
    if (*out_fd == -1) {
        CloseHandle(h);
        return VERIFY_ERR_PARENT;
    }
#else
    /* POSIX: Open with O_DIRECTORY */
    int fd = open(approved->parent_path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        return VERIFY_ERR_PARENT;
    }

    /* Verify inode/device match */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return VERIFY_ERR_STAT;
    }

    if (st.st_ino != approved->parent_inode || st.st_dev != approved->parent_device) {
        close(fd);
        return VERIFY_ERR_PARENT_CHANGED;
    }

    *out_fd = fd;
#endif

    return VERIFY_OK;
}

/* ============================================================================
 * Atomic File Creation
 * ========================================================================== */

VerifyResult create_file_in_verified_parent(const ApprovedPath *approved,
                                            int flags,
                                            mode_t mode,
                                            int *out_fd) {
    if (!approved || !out_fd) {
        return VERIFY_ERR_INVALID_PATH;
    }

    *out_fd = -1;

    /* File should not already exist for this function */
    if (approved->existed) {
        return VERIFY_ERR_ALREADY_EXISTS;
    }

    /* Open and verify parent directory */
    int parent_fd = -1;
    VerifyResult parent_result = open_verified_parent(approved, &parent_fd);
    if (parent_result != VERIFY_OK) {
        return parent_result;
    }

    /* Get basename for openat */
    const char *base = atomic_file_basename(approved->user_path);
    if (!base || !*base) {
        close(parent_fd);
        return VERIFY_ERR_INVALID_PATH;
    }

#ifdef _WIN32
    /* Windows: Need to construct full path and use CreateFile */
    close(parent_fd); /* We verified parent, now use full path */

    HANDLE h = CreateFileA(
        approved->resolved_path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_NEW,  /* Equivalent to O_EXCL */
        FILE_FLAG_OPEN_REPARSE_POINT,
        NULL
    );

    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS) {
            return VERIFY_ERR_ALREADY_EXISTS;
        }
        return VERIFY_ERR_CREATE;
    }

    *out_fd = _open_osfhandle((intptr_t)h, flags);
    if (*out_fd == -1) {
        CloseHandle(h);
        return VERIFY_ERR_CREATE;
    }
#else
    /* POSIX: Use openat for atomic creation relative to verified parent */
    int open_flags = flags | O_CREAT | O_EXCL | O_NOFOLLOW;
    int fd = openat(parent_fd, base, open_flags, mode);
    close(parent_fd);

    if (fd < 0) {
        if (errno == EEXIST) {
            return VERIFY_ERR_ALREADY_EXISTS;
        }
        return VERIFY_ERR_CREATE;
    }

    *out_fd = fd;
#endif

    return VERIFY_OK;
}
