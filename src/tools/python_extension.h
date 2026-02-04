/**
 * src/tools/python_extension.h - Python Tool Extension
 *
 * Provides the registration function for the Python tool extension.
 */

#ifndef PYTHON_EXTENSION_H
#define PYTHON_EXTENSION_H

/**
 * Register the Python extension with the tool extension system.
 * Call this early in main() before session_init().
 *
 * @return 0 on success, -1 on failure
 */
int python_extension_register(void);

#endif /* PYTHON_EXTENSION_H */
