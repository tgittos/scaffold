#ifndef PROJECT_SCOPE_H
#define PROJECT_SCOPE_H

#include <stddef.h>

/* Large enough for any name project_scope_index_name() produces with a
 * prefix of up to 32 characters. */
#define PROJECT_SCOPE_NAME_MAX 96

/**
 * Build a per-project index name: "<prefix>_<basename>_<hash>".
 *
 * The project root is the nearest ancestor of dir (inclusive) containing a
 * .git entry (directory or worktree file), or dir itself when none exists.
 * The hash is FNV-1a 64 of the resolved absolute root path, so directories
 * with the same basename get distinct names.
 *
 * @return 0 on success, -1 if dir cannot be resolved or out is too small
 */
int project_scope_index_name_for(const char* dir, const char* prefix,
                                 char* out, size_t out_size);

/** project_scope_index_name_for() applied to the current working directory. */
int project_scope_index_name(const char* prefix, char* out, size_t out_size);

/**
 * Resolve the nearest ancestor of dir (inclusive) containing a .git entry.
 *
 * @return 0 on success, -1 if dir cannot be resolved, no ancestor holds
 *         .git, or out is too small
 */
int project_scope_git_root(const char* dir, char* out, size_t out_size);

#endif
