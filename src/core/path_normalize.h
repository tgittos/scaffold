#ifndef PATH_NORMALIZE_H
#define PATH_NORMALIZE_H

/**
 * Cross-Platform Path Normalization
 *
 * Normalizes filesystem paths for consistent comparison across platforms:
 * - Windows: converts backslashes to forward slashes, lowercases path,
 *   normalizes drive letters (C: -> /c/), handles UNC paths
 * - POSIX: minimal normalization (case-sensitive)
 * - Both: removes trailing slashes, collapses duplicate slashes
 *
 * This module is used by the approval gates system for protected file
 * detection and allowlist pattern matching.
 */

/**
 * Normalized path representation.
 * All paths are normalized to forward slashes for consistent matching.
 */
typedef struct {
    char *normalized;   /* Normalized path string (allocated, caller owns) */
    char *basename;     /* Pointer into normalized for final component (not allocated) */
    int is_absolute;    /* Non-zero if path is absolute */
} NormalizedPath;

/**
 * Normalize a filesystem path for cross-platform comparison.
 *
 * Normalization rules:
 *
 * Windows:
 *   - Backslashes converted to forward slashes
 *   - Entire path lowercased (case-insensitive FS)
 *   - Drive letters converted: C:\foo -> /c/foo
 *   - UNC paths converted: \\server\share -> /unc/server/share
 *
 * POSIX:
 *   - No case conversion (case-sensitive FS)
 *   - Already uses forward slashes
 *
 * Both:
 *   - Trailing slashes removed (except root)
 *   - Duplicate slashes collapsed
 *   - Basename extracted (final path component)
 *
 * @param path Input path to normalize
 * @return Allocated NormalizedPath structure, or NULL on error.
 *         Caller must free with free_normalized_path().
 */
NormalizedPath *normalize_path(const char *path);

/**
 * Free a NormalizedPath structure.
 *
 * @param np Path to free (NULL safe)
 */
void free_normalized_path(NormalizedPath *np);

/**
 * Compare two basenames using platform-appropriate case sensitivity.
 * Case-insensitive on Windows, case-sensitive on POSIX.
 *
 * @param a First basename
 * @param b Second basename
 * @return 0 if equal, non-zero otherwise
 */
int path_basename_cmp(const char *a, const char *b);

/**
 * Check if a basename starts with a prefix using platform-appropriate
 * case sensitivity.
 *
 * @param basename The basename to check
 * @param prefix The prefix to match
 * @return 1 if basename starts with prefix, 0 otherwise
 */
int path_basename_has_prefix(const char *basename, const char *prefix);

#endif /* PATH_NORMALIZE_H */
