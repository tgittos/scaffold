#include "sys_python.h"
#include "util/executable_path.h"
#include "util/app_home.h"
#include <Python.h>
#include <stdlib.h>

/**
 * _ralph_sys.get_executable_path()
 *
 * Returns the path to the current executable.
 */
static PyObject *py_get_executable_path(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    char *path = get_executable_path();
    if (path == NULL) {
        Py_RETURN_NONE;
    }

    PyObject *result = PyUnicode_FromString(path);
    free(path);
    return result;
}

/**
 * _ralph_sys.get_app_home()
 *
 * Returns the application home directory path (e.g. ~/.local/scaffold).
 * Returns None if app_home is not initialized.
 */
static PyObject *py_get_app_home(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    const char *home = app_home_get();
    if (home == NULL) {
        Py_RETURN_NONE;
    }

    return PyUnicode_FromString(home);
}

static PyMethodDef SysMethods[] = {
    {"get_executable_path", py_get_executable_path, METH_NOARGS,
     "Get the path to the current executable."},
    {"get_app_home", py_get_app_home, METH_NOARGS,
     "Get the application home directory path."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef sys_module = {
    PyModuleDef_HEAD_INIT,
    "_ralph_sys",
    "System information for ralph tools.",
    -1,
    SysMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC PyInit__ralph_sys(void) {
    return PyModule_Create(&sys_module);
}

int sys_python_init(void) {
    if (PyImport_AppendInittab("_ralph_sys", PyInit__ralph_sys) == -1) {
        return -1;
    }
    return 0;
}
