#include "sys_python.h"
#include "util/executable_path.h"
#include "util/app_home.h"
#include "util/config.h"
#include "util/interrupt.h"
#include "ui/spinner.h"
#include <Python.h>
#include <stdlib.h>

/*
 * Sink for progress lines forwarded from Python tools. Defaults to the
 * spinner's live detail line; tests may override it to capture the lines.
 */
typedef void (*progress_sink_fn)(const char *line);
static progress_sink_fn g_progress_sink = spinner_set_detail;

void sys_python_set_progress_sink_for_test(progress_sink_fn fn) {
    g_progress_sink = fn ? fn : spinner_set_detail;
}

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

/**
 * _ralph_sys.get_config_int(key, default)
 *
 * Returns the integer config value for key, or default if the key is
 * unknown or config is not initialized.
 */
static PyObject *py_get_config_int(PyObject *self, PyObject *args) {
    (void)self;

    const char *key = NULL;
    int default_value = 0;
    if (!PyArg_ParseTuple(args, "si", &key, &default_value)) {
        return NULL;
    }

    return PyLong_FromLong(config_get_int(key, default_value));
}

/**
 * _ralph_sys.progress(line)
 *
 * Forwards a single output line to the live progress sink (the spinner's
 * detail line by default). Called from tool worker threads as output
 * streams in; the string is converted to C here so no per-byte round trips
 * cross the C/Python boundary.
 */
static PyObject *py_progress(PyObject *self, PyObject *args) {
    (void)self;

    const char *line = NULL;
    if (!PyArg_ParseTuple(args, "s", &line)) {
        return NULL;
    }

    if (line != NULL) {
        g_progress_sink(line);
    }

    Py_RETURN_NONE;
}

/**
 * _ralph_sys.cancelled()
 *
 * Returns 1 if a user cancellation (Ctrl-C mid-turn) is currently
 * latched, else 0. Tools that block in long waits can poll this in a
 * short-timeout loop to abort early; the flag is latched by the REPL's
 * cancel path and cleared when the turn ends, so no state is kept here.
 */
static PyObject *py_cancelled(PyObject *self, PyObject *args) {
    (void)self;
    (void)args;

    return PyLong_FromLong(interrupt_pending());
}

static PyMethodDef SysMethods[] = {
    {"get_executable_path", py_get_executable_path, METH_NOARGS,
     "Get the path to the current executable."},
    {"get_app_home", py_get_app_home, METH_NOARGS,
     "Get the application home directory path."},
    {"get_config_int", py_get_config_int, METH_VARARGS,
     "Get an integer config value, or the given default."},
    {"progress", py_progress, METH_VARARGS,
     "Report a progress line to the live spinner detail."},
    {"cancelled", py_cancelled, METH_NOARGS,
     "Return 1 if a user cancellation is currently latched."},
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
