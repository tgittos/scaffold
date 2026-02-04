#ifndef RALPH_HOME_H
#define RALPH_HOME_H

/**
 * ralph_home - Centralized home directory management for Ralph
 *
 * Provides a single source of truth for the Ralph home directory path.
 * Priority order:
 *   1. CLI flag --home <path> (highest)
 *   2. Environment variable RALPH_HOME
 *   3. Default ~/.local/ralph (lowest)
 *
 * All modules that need to access Ralph's data directory should use this API
 * instead of constructing paths directly.
 */

/**
 * Initialize the Ralph home directory.
 *
 * Must be called early in main() before any other initialization.
 * Resolves the home directory path based on priority:
 *   1. cli_override parameter (if provided)
 *   2. RALPH_HOME environment variable
 *   3. Default $HOME/.local/ralph
 *
 * Relative paths are resolved to absolute paths using getcwd().
 *
 * @param cli_override Path from --home flag, or NULL if not provided
 * @return 0 on success, -1 on failure
 */
int ralph_home_init(const char *cli_override);

/**
 * Get the Ralph home directory path.
 *
 * @return Absolute path to Ralph home directory, or NULL if not initialized
 */
const char* ralph_home_get(void);

/**
 * Get a path within the Ralph home directory.
 *
 * Constructs a full path by joining the home directory with the relative path.
 * Example: ralph_home_path("tasks.db") returns "/home/user/.local/ralph/tasks.db"
 *
 * @param relative_path Path relative to Ralph home (without leading slash)
 * @return Newly allocated full path string (caller must free), or NULL on error
 */
char* ralph_home_path(const char *relative_path);

/**
 * Ensure the Ralph home directory exists.
 *
 * Creates the home directory and any necessary parent directories.
 *
 * @return 0 on success, -1 on failure
 */
int ralph_home_ensure_exists(void);

/**
 * Clean up the Ralph home module.
 *
 * Frees any allocated memory. Should be called during shutdown.
 */
void ralph_home_cleanup(void);

/**
 * Check if Ralph home has been initialized.
 *
 * @return 1 if initialized, 0 otherwise
 */
int ralph_home_is_initialized(void);

#endif // RALPH_HOME_H
