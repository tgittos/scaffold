/**
 * Python Extension for Verified File Access
 *
 * Exposes the verified file context to Python, allowing Python-based
 * file tools to use TOCTOU-safe file operations.
 *
 * Python Usage:
 *   import _ralph_verified_io
 *
 *   # Check if verified context is available
 *   if _ralph_verified_io.has_verified_context():
 *       # Get a verified file descriptor
 *       fd = _ralph_verified_io.open_verified(path, "w")
 *       f = os.fdopen(fd, "w")
 *       f.write(content)
 *       f.close()
 *   else:
 *       # Fall back to regular file I/O
 *       with open(path, "w") as f:
 *           f.write(content)
 */

#ifndef VERIFIED_FILE_PYTHON_H
#define VERIFIED_FILE_PYTHON_H

/**
 * Initialize the _ralph_verified_io Python extension module.
 *
 * Must be called before Py_Initialize() to register the module.
 * The module provides:
 *   - has_verified_context() -> bool
 *   - open_verified(path, mode) -> int (file descriptor)
 *   - get_resolved_path() -> str or None
 *
 * @return 0 on success, -1 on failure
 */
int verified_file_python_init(void);

#endif /* VERIFIED_FILE_PYTHON_H */
