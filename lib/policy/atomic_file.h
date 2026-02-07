#ifndef ATOMIC_FILE_H
#define ATOMIC_FILE_H

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Atomic File Operations Module
 *
 * Provides TOCTOU-safe (time-of-check-to-time-of-use) file operations for
 * the approval gates system. When a user approves a file operation, this
 * module ensures the file hasn't changed between approval and execution.
 *
 * Threat Model:
 * TOCTOU attacks exploit the gap between checking a path and using it:
 *   1. User approves write to ./safe.txt
 *   2. Attacker replaces ./safe.txt with symlink to /etc/passwd
 *   3. Write executes against the symlink target
 *
 * Simple realpath() + stat() checks are insufficient because the filesystem
 * can change between the check and the operation.
 *
 * Protection Strategy:
 * - For existing files: Open with O_NOFOLLOW, verify inode matches approval
 * - For new files: Verify parent directory inode, create with O_EXCL
 * - Use openat() for atomic parent-relative operations
 * - Track file identity via inode/device (POSIX) or file index (Windows)
 *
 * Platform Notes:
 * - POSIX: Uses O_NOFOLLOW, O_EXCL, O_DIRECTORY, openat(), fstat()
 * - Windows: Uses FILE_FLAG_OPEN_REPARSE_POINT, CREATE_NEW,
 *   GetFileInformationByHandle() for file identity
 * - Network FS: Has weaker guarantees; module sets is_network_fs flag
 *
 * Related headers:
 * - approval_gate.h: Integration with approval gates system
 * - path_normalize.h: Cross-platform path normalization
 *
 */

/**
 * Result codes from atomic file verification operations.
 */
typedef enum {
    VERIFY_OK = 0,              /* Path verified successfully */
    VERIFY_ERR_SYMLINK,         /* Path is a symlink (rejected by O_NOFOLLOW) */
    VERIFY_ERR_DELETED,         /* File was deleted after approval */
    VERIFY_ERR_OPEN,            /* Failed to open file */
    VERIFY_ERR_STAT,            /* Failed to stat file */
    VERIFY_ERR_INODE_MISMATCH,  /* Inode/device changed since approval */
    VERIFY_ERR_PARENT,          /* Cannot open parent directory */
    VERIFY_ERR_PARENT_CHANGED,  /* Parent directory inode changed */
    VERIFY_ERR_ALREADY_EXISTS,  /* File exists when creating new file (O_EXCL) */
    VERIFY_ERR_CREATE,          /* Failed to create new file */
    VERIFY_ERR_INVALID_PATH,    /* Path is NULL or malformed */
    VERIFY_ERR_RESOLVE,         /* Failed to resolve path */
    VERIFY_ERR_NETWORK_FS       /* Network filesystem, verification unreliable */
} VerifyResult;

/**
 * Approved path with TOCTOU protection data.
 *
 * Captures filesystem state at approval time. At execution time,
 * the actual filesystem state is compared against these values
 * to detect TOCTOU attacks.
 *
 * For existing files:
 *   - inode and device are populated from stat() at approval
 *   - At execution, file is opened with O_NOFOLLOW, then fstat() verifies match
 *
 * For new files:
 *   - parent_inode and parent_device capture parent directory state
 *   - At execution, parent is verified, then file created with O_EXCL
 */
typedef struct {
    /* User-provided path */
    char *user_path;            /* Original path from tool call (allocated) */
    char *resolved_path;        /* Canonical path at approval time (allocated) */

    /* Existing file identity (POSIX) */
    ino_t inode;                /* Inode at approval (0 if new file) */
    dev_t device;               /* Device at approval */

    /* Parent directory identity (for new files) */
    ino_t parent_inode;         /* Parent directory inode */
    dev_t parent_device;        /* Parent directory device */
    char *parent_path;          /* Resolved parent path (allocated) */

    /* Flags */
    int existed;                /* Non-zero if file existed at approval time */
    int is_network_fs;          /* Non-zero if detected as NFS/CIFS/SMB */

#ifdef _WIN32
    /* Windows file identity */
    DWORD volume_serial;        /* Volume serial number */
    DWORD index_high;           /* File index (high DWORD) */
    DWORD index_low;            /* File index (low DWORD) */

    /* Windows parent directory identity */
    DWORD parent_volume_serial;
    DWORD parent_index_high;
    DWORD parent_index_low;
#endif
} ApprovedPath;

/**
 * Capture filesystem state for an approved path.
 *
 * Call this at approval time to populate an ApprovedPath struct with
 * the current filesystem state. The struct can then be used at execution
 * time with verify_and_open_approved_path().
 *
 * @param path User-provided path
 * @param out Output: ApprovedPath structure to populate
 * @return VERIFY_OK on success, or error code
 */
VerifyResult capture_approved_path(const char *path, ApprovedPath *out);

/**
 * Initialize an ApprovedPath structure to safe defaults.
 *
 * @param ap Structure to initialize
 */
void init_approved_path(ApprovedPath *ap);

/**
 * Free resources held by an ApprovedPath structure.
 * Does not free the structure itself.
 *
 * @param ap Path to clean up (NULL safe)
 */
void free_approved_path(ApprovedPath *ap);

/**
 * Verify that an approved path hasn't changed since approval.
 *
 * Performs a non-opening verification check. For full TOCTOU protection,
 * use verify_and_open_approved_path() instead which atomically verifies
 * and opens the file.
 *
 * @param approved The approved path data
 * @return VERIFY_OK if unchanged, or specific error code
 */
VerifyResult verify_approved_path(const ApprovedPath *approved);

/**
 * Verify and open an approved path atomically.
 *
 * This is the main TOCTOU protection function. It:
 * - Opens the file with O_NOFOLLOW to reject final-component symlinks
 * - Verifies inode/device match the approved values via fstat()
 * - For new files, verifies parent and creates atomically with O_EXCL
 *
 * The returned file descriptor should be used for the operation instead
 * of re-opening the path, which would create a new TOCTOU window.
 *
 * @param approved The approved path data
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, optionally O_APPEND)
 * @param out_fd Output: file descriptor on success (-1 on failure)
 * @return VERIFY_OK on success, or specific error code
 */
VerifyResult verify_and_open_approved_path(const ApprovedPath *approved,
                                           int flags,
                                           int *out_fd);

/**
 * Create a new file atomically in a verified parent directory.
 *
 * Uses openat() with O_CREAT | O_EXCL to atomically create a file
 * after verifying the parent directory matches expectations.
 *
 * @param approved The approved path data (must have existed == 0)
 * @param flags Additional open flags (O_WRONLY or O_RDWR required)
 * @param mode File creation mode (e.g., 0644)
 * @param out_fd Output: file descriptor on success
 * @return VERIFY_OK on success, or specific error code
 */
VerifyResult create_file_in_verified_parent(const ApprovedPath *approved,
                                            int flags,
                                            mode_t mode,
                                            int *out_fd);

/**
 * Open and verify a parent directory.
 *
 * Opens the directory with O_DIRECTORY and verifies its identity.
 * Used internally by create_file_in_verified_parent().
 *
 * @param approved The approved path data
 * @param out_fd Output: directory file descriptor on success
 * @return VERIFY_OK on success, or specific error code
 */
VerifyResult open_verified_parent(const ApprovedPath *approved,
                                  int *out_fd);

/**
 * Check if a path is on a network filesystem.
 *
 * Network filesystems (NFS, CIFS/SMB) have weaker guarantees:
 * - Inode numbers may be reused
 * - O_NOFOLLOW support may be incomplete
 * - Attribute caching can cause stale reads
 *
 * When is_network_fs is set, callers may want to warn the user
 * that verification is unreliable.
 *
 * Detection methods:
 * - Linux: Check /proc/mounts for nfs, cifs, smb filesystem types
 * - Windows: Use GetVolumeInformation() to check drive type
 * - Other POSIX: Check statvfs() for known network FS magic numbers
 *
 * @param path Path to check
 * @return 1 if network filesystem, 0 otherwise
 */
int is_network_filesystem(const char *path);

/**
 * Get a human-readable message for a verification result.
 *
 * @param result The verification result code
 * @return Static string describing the error
 */
const char *verify_result_message(VerifyResult result);

/**
 * Format a verification error as JSON.
 *
 * @param result The verification result
 * @param path The path that failed verification
 * @return Allocated JSON error string. Caller must free.
 */
char *format_verify_error(VerifyResult result, const char *path);

/**
 * Extract the basename from a path.
 *
 * Returns a pointer to the final component of the path.
 * Does not allocate memory - returns pointer into the input string.
 *
 * @param path The path
 * @return Pointer to the basename within path
 */
const char *atomic_file_basename(const char *path);

/**
 * Extract the parent directory from a path.
 *
 * Examples:
 *   "/foo/bar" -> "/foo"
 *   "/foo"     -> "/"
 *   "foo/bar"  -> "foo"
 *   "foo"      -> "."
 *   "/"        -> "/"
 *
 * @param path The path
 * @return Allocated string containing parent directory. Caller must free.
 *         Returns "/" for paths directly under root, "." for relative paths
 *         with no directory component.
 */
char *atomic_file_dirname(const char *path);

/**
 * Resolve a path to its canonical form.
 *
 * On POSIX, uses realpath() for existing files.
 * For new files, resolves the parent directory and appends the basename.
 *
 * @param path The path to resolve
 * @param must_exist If non-zero, path must exist
 * @return Allocated canonical path, or NULL on error. Caller must free.
 */
char *atomic_file_resolve_path(const char *path, int must_exist);

#endif /* ATOMIC_FILE_H */
