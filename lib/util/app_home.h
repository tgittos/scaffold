#ifndef APP_HOME_H
#define APP_HOME_H

/**
 * app_home - Centralized home directory management
 *
 * Provides a single source of truth for the application home directory path.
 * The app name determines both the default directory and environment variable:
 *   - Default dir: ~/.local/<app_name>
 *   - Env var: <APP_NAME>_HOME (uppercased)
 *
 * Priority order:
 *   1. CLI flag --home <path> (highest)
 *   2. Environment variable <APP_NAME>_HOME
 *   3. Default ~/.local/<app_name> (lowest)
 *
 * All modules that need to access the data directory should use this API
 * instead of constructing paths directly.
 */

/**
 * Set the application name before calling app_home_init().
 *
 * Determines:
 *   - Default dir: ~/.local/<name>
 *   - Env var: <NAME>_HOME (uppercased)
 *
 * If never called, defaults to "ralph" for backwards compatibility.
 *
 * @param name Application name (e.g. "ralph", "scaffold")
 */
void app_home_set_app_name(const char *name);

/**
 * Initialize the application home directory.
 *
 * Must be called early in main() before any other initialization.
 * Resolves the home directory path based on priority:
 *   1. cli_override parameter (if provided)
 *   2. <APP_NAME>_HOME environment variable
 *   3. Default $HOME/.local/<app_name>
 *
 * Relative paths are resolved to absolute paths using getcwd().
 *
 * @param cli_override Path from --home flag, or NULL if not provided
 * @return 0 on success, -1 on failure
 */
int app_home_init(const char *cli_override);

/**
 * Get the application home directory path.
 *
 * @return Absolute path to home directory, or NULL if not initialized
 */
const char* app_home_get(void);

/**
 * Get a path within the application home directory.
 *
 * Constructs a full path by joining the home directory with the relative path.
 * Example: app_home_path("tasks.db") returns "/home/user/.local/ralph/tasks.db"
 *
 * @param relative_path Path relative to home (without leading slash)
 * @return Newly allocated full path string (caller must free), or NULL on error
 */
char* app_home_path(const char *relative_path);

/**
 * Ensure the application home directory exists.
 *
 * Creates the home directory and any necessary parent directories.
 *
 * @return 0 on success, -1 on failure
 */
int app_home_ensure_exists(void);

/**
 * Clean up the app_home module.
 *
 * Frees any allocated memory. Should be called during shutdown.
 */
void app_home_cleanup(void);

/**
 * Check if app_home has been initialized.
 *
 * @return 1 if initialized, 0 otherwise
 */
int app_home_is_initialized(void);

/**
 * Get the current application name.
 *
 * @return Application name (e.g. "ralph", "scaffold"), never NULL
 */
const char* app_home_get_app_name(void);

#endif // APP_HOME_H
