#ifndef PROTECTED_FILES_H
#define PROTECTED_FILES_H

#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Protected Files Detection Module
 *
 * Detects and blocks modification of protected configuration files:
 * - ralph.config.json (and **/ralph.config.json)
 * - .ralph/config.json
 * - .env files (.env, .env.*, etc.)
 *
 * This protection is enforced at the tool execution layer and cannot be
 * bypassed by gate configuration or allowlist settings.
 *
 * Detection strategies:
 * 1. Basename exact match (e.g., "ralph.config.json", ".env")
 * 2. Basename prefix match (e.g., ".env.*")
 * 3. Glob pattern match (e.g., "**/.ralph/config.json")
 * 4. Inode-based detection (catches hardlinks and renames)
 *
 * The inode cache is refreshed periodically to detect late-created files.
 * On Windows, file identity uses volume serial + file index.
 *
 * Related headers:
 * - path_normalize.h: Cross-platform path normalization for matching
 * - approval_gate.h: Integration with the approval gates system
 */

/* Refresh interval for inode cache (seconds) */
#define PROTECTED_INODE_REFRESH_INTERVAL 30

/**
 * Tracked inode for a protected file.
 * Used to detect hardlinks, renames, and late-created files.
 */
typedef struct {
    dev_t device;           /* Device ID (POSIX) */
    ino_t inode;            /* Inode number (POSIX) */
#ifdef _WIN32
    DWORD volume_serial;    /* Windows volume serial number */
    DWORD index_high;       /* Windows file index (high DWORD) */
    DWORD index_low;        /* Windows file index (low DWORD) */
#endif
    char *original_path;    /* Path when first discovered (for debugging) */
    time_t discovered_at;   /* When this inode was recorded */
} ProtectedInode;

/**
 * Cache of protected file inodes.
 * Periodically refreshed to catch newly-created protected files.
 */
typedef struct {
    ProtectedInode *inodes; /* Dynamic array of tracked inodes */
    int count;              /* Number of entries in use */
    int capacity;           /* Allocated capacity */
    time_t last_refresh;    /* Timestamp of last cache refresh */
} ProtectedInodeCache;

/* ============================================================================
 * Core Detection Functions
 * ========================================================================== */

/**
 * Check if a path points to a protected file.
 *
 * Protected files cannot be modified by any tool, regardless of gate
 * configuration or allowlist settings. This function performs:
 * 1. Basename matching against known protected filenames
 * 2. Prefix matching for .env.* files
 * 3. Glob pattern matching for path patterns
 * 4. Inode comparison to catch hardlinks/renames
 *
 * The inode cache is automatically refreshed if stale.
 *
 * @param path The file path to check (will be normalized internally)
 * @return 1 if protected, 0 if not protected
 */
int is_protected_file(const char *path);

/**
 * Check if a basename matches a protected file pattern.
 * Uses platform-appropriate case sensitivity.
 *
 * @param basename The filename to check (final path component only)
 * @return 1 if protected, 0 if not protected
 */
int is_protected_basename(const char *basename);

/**
 * Check if a path matches a protected glob pattern.
 * Uses fnmatch with FNM_CASEFOLD on Windows.
 *
 * @param path Normalized path to check
 * @return 1 if matches protected pattern, 0 if not
 */
int matches_protected_glob(const char *path);

/**
 * Check if a file's inode is in the protected inode cache.
 *
 * @param path Path to the file
 * @return 1 if inode is protected, 0 if not
 */
int is_protected_inode(const char *path);

/* ============================================================================
 * Inode Cache Management
 * ========================================================================== */

/**
 * Refresh the protected inode cache if stale.
 *
 * Scans common locations for protected files and updates the inode cache.
 * Called automatically by is_protected_file() when the cache is older than
 * PROTECTED_INODE_REFRESH_INTERVAL seconds.
 *
 * Scanned locations include:
 * - ralph.config.json in current directory
 * - .ralph/config.json
 * - .env, .env.local, .env.development, .env.production, .env.test
 * - Parent directories up to 3 levels
 */
void refresh_protected_inodes(void);

/**
 * Force an immediate refresh of the protected inode cache.
 * Call before processing a batch of potentially destructive tool calls.
 *
 * This ensures that protected files created since the last refresh
 * (e.g., a .env file created mid-session) are detected.
 */
void force_protected_inode_refresh(void);

/**
 * Add a file to the protected inode cache if it exists.
 *
 * @param path Path to the file to track
 */
void add_protected_inode_if_exists(const char *path);

/**
 * Clear all entries from the protected inode cache.
 * Does not free the cache structure itself.
 */
void clear_protected_inode_cache(void);

/**
 * Free all resources held by the protected inode cache.
 * Should be called during cleanup.
 */
void cleanup_protected_inode_cache(void);

/* ============================================================================
 * Protected Patterns Configuration
 * ========================================================================== */

/**
 * Get the array of protected basename patterns.
 * These are exact filenames that are always protected.
 *
 * @param count Output: number of patterns in array
 * @return Pointer to static array of pattern strings (NULL-terminated)
 */
const char **get_protected_basename_patterns(int *count);

/**
 * Get the array of protected prefix patterns.
 * Basenames starting with these prefixes are protected (e.g., ".env.").
 *
 * @param count Output: number of patterns in array
 * @return Pointer to static array of pattern strings (NULL-terminated)
 */
const char **get_protected_prefix_patterns(int *count);

/**
 * Get the array of protected glob patterns.
 * Full paths matching these globs are protected.
 *
 * @param count Output: number of patterns in array
 * @return Pointer to static array of pattern strings (NULL-terminated)
 */
const char **get_protected_glob_patterns(int *count);

/* ============================================================================
 * Initialization and Cleanup
 * ========================================================================== */

/**
 * Initialize the protected files module.
 * Called automatically on first use, but can be called explicitly.
 *
 * @return 0 on success, -1 on failure
 */
int protected_files_init(void);

/**
 * Clean up the protected files module.
 * Frees all cached data and resets state.
 */
void protected_files_cleanup(void);

/* ============================================================================
 * Error Formatting
 * ========================================================================== */

/**
 * Format a protected file error message as JSON.
 *
 * @param path The protected file path
 * @return Allocated JSON error string. Caller must free.
 *
 * Example output:
 * {
 *   "error": "protected_file",
 *   "message": "Cannot modify protected configuration file",
 *   "path": "ralph.config.json"
 * }
 */
char *format_protected_file_error(const char *path);

#endif /* PROTECTED_FILES_H */
