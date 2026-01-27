#include "verified_file_python.h"
#include "verified_file_context.h"
#include <Python.h>
#include <string.h>

/**
 * Python function: _ralph_verified_io.has_verified_context()
 *
 * Returns True if a verified file context is currently active.
 */
static PyObject *py_has_verified_context(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    if (verified_file_context_is_set()) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

/**
 * Python function: _ralph_verified_io.open_verified(path, mode)
 *
 * Opens a file using TOCTOU-safe verification.
 *
 * Args:
 *   path: The file path to open
 *   mode: Open mode string ("r", "w", "a", "r+")
 *
 * Returns:
 *   File descriptor (int) on success
 *
 * Raises:
 *   OSError: If the file cannot be opened or verification fails
 *   ValueError: If the mode is invalid
 */
static PyObject *py_open_verified(PyObject *self, PyObject *args) {
    (void)self;

    const char *path = NULL;
    const char *mode = NULL;

    if (!PyArg_ParseTuple(args, "ss", &path, &mode)) {
        return NULL;
    }

    VerifiedFileMode file_mode;
    if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0) {
        file_mode = VERIFIED_MODE_READ;
    } else if (strcmp(mode, "w") == 0 || strcmp(mode, "wb") == 0) {
        file_mode = VERIFIED_MODE_WRITE;
    } else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0) {
        file_mode = VERIFIED_MODE_APPEND;
    } else if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0 ||
               strcmp(mode, "r+b") == 0 || strcmp(mode, "w+") == 0 ||
               strcmp(mode, "wb+") == 0 || strcmp(mode, "w+b") == 0 ||
               strcmp(mode, "a+") == 0 || strcmp(mode, "ab+") == 0 ||
               strcmp(mode, "a+b") == 0) {
        file_mode = VERIFIED_MODE_READWRITE;
    } else {
        PyErr_Format(PyExc_ValueError, "Invalid mode: '%s'. "
                     "Supported modes: r, w, a, r+, a+, rb, wb, ab, r+b, rb+, w+, wb+, w+b, a+b, ab+",
                     mode);
        return NULL;
    }

    int fd = -1;
    VerifyResult result = verified_file_context_get_fd(path, file_mode, &fd);

    if (result != VERIFY_OK) {
        const char *error_msg = verify_result_message(result);
        PyErr_Format(PyExc_OSError, "Failed to open '%s': %s", path, error_msg);
        return NULL;
    }

    return PyLong_FromLong(fd);
}

/**
 * Python function: _ralph_verified_io.get_resolved_path()
 *
 * Returns the resolved (canonical) path from the current verified context.
 *
 * Returns:
 *   str: The resolved path
 *   None: If no context is active
 */
static PyObject *py_get_resolved_path(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    const char *path = verified_file_context_get_resolved_path();
    if (path == NULL) {
        Py_RETURN_NONE;
    }

    return PyUnicode_FromString(path);
}

/**
 * Python function: _ralph_verified_io.path_matches(requested_path)
 *
 * Check if a path matches the currently approved path.
 *
 * Args:
 *   requested_path: The path to check
 *
 * Returns:
 *   bool: True if the path matches, False otherwise
 */
static PyObject *py_path_matches(PyObject *self, PyObject *args) {
    (void)self;

    const char *path = NULL;
    if (!PyArg_ParseTuple(args, "s", &path)) {
        return NULL;
    }

    if (verified_file_context_path_matches(path)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef VerifiedIOMethods[] = {
    {"has_verified_context", py_has_verified_context, METH_NOARGS,
     "Check if a verified file context is active."},
    {"open_verified", py_open_verified, METH_VARARGS,
     "Open a file with TOCTOU-safe verification. Returns file descriptor."},
    {"get_resolved_path", py_get_resolved_path, METH_NOARGS,
     "Get the resolved path from the current verified context."},
    {"path_matches", py_path_matches, METH_VARARGS,
     "Check if a path matches the currently approved path."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef verified_io_module = {
    PyModuleDef_HEAD_INIT,
    "_ralph_verified_io",
    "TOCTOU-safe file operations for ralph tools.",
    -1,
    VerifiedIOMethods,
    NULL,  /* m_slots */
    NULL,  /* m_traverse */
    NULL,  /* m_clear */
    NULL   /* m_free */
};

PyMODINIT_FUNC PyInit__ralph_verified_io(void) {
    return PyModule_Create(&verified_io_module);
}

int verified_file_python_init(void) {
    /* Must be called before Py_Initialize(). */
    if (PyImport_AppendInittab("_ralph_verified_io", PyInit__ralph_verified_io) == -1) {
        return -1;
    }
    return 0;
}
