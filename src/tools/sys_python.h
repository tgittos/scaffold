/**
 * Python Extension for System Information
 *
 * Exposes application paths to Python, allowing Python-based tools
 * to discover the executable location and app home directory.
 *
 * Python Usage:
 *   import _ralph_sys
 *
 *   exe = _ralph_sys.get_executable_path()
 *   # exe = "/usr/local/bin/scaffold"
 *
 *   home = _ralph_sys.get_app_home()
 *   # home = "/home/user/.local/scaffold"
 */

#ifndef SYS_PYTHON_H
#define SYS_PYTHON_H

/**
 * Initialize the _ralph_sys Python extension module.
 *
 * Must be called before Py_Initialize() to register the module.
 * The module provides:
 *   - get_executable_path() -> str
 *   - get_app_home() -> str or None
 *
 * @return 0 on success, -1 on failure
 */
int sys_python_init(void);

#endif /* SYS_PYTHON_H */
