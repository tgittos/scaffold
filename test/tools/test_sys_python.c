#include "unity/unity.h"
#include "sys_python.h"
#include "util/app_home.h"
#include <Python.h>
#include <string.h>
#include <stdlib.h>

static int python_initialized = 0;

void setUp(void) {
    if (!python_initialized) {
        app_home_init(NULL);
        TEST_ASSERT_EQUAL_INT(0, sys_python_init());

        setenv("PYTHONHOME", "/zip", 1);
        setenv("PYTHONDONTWRITEBYTECODE", "1", 1);

        Py_Initialize();
        TEST_ASSERT_TRUE(Py_IsInitialized());
        python_initialized = 1;
    }
}

void tearDown(void) {
}

static PyObject *run_python(const char *code) {
    PyObject *main_dict = PyModule_GetDict(PyImport_AddModule("__main__"));
    PyObject *result = PyRun_String(code, Py_file_input, main_dict, main_dict);
    if (result == NULL) {
        PyErr_Print();
    }
    return result;
}

static const char *get_python_str(const char *varname) {
    PyObject *main_dict = PyModule_GetDict(PyImport_AddModule("__main__"));
    PyObject *val = PyDict_GetItemString(main_dict, varname);
    if (val == NULL) return NULL;
    return PyUnicode_AsUTF8(val);
}

void test_sys_module_imports(void) {
    PyObject *module = PyImport_ImportModule("_ralph_sys");
    TEST_ASSERT_NOT_NULL_MESSAGE(module, "Failed to import _ralph_sys");
    Py_DECREF(module);
}

void test_get_executable_path_exists(void) {
    PyObject *module = PyImport_ImportModule("_ralph_sys");
    TEST_ASSERT_NOT_NULL(module);

    PyObject *func = PyObject_GetAttrString(module, "get_executable_path");
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "get_executable_path not found");
    TEST_ASSERT_TRUE(PyCallable_Check(func));

    Py_DECREF(func);
    Py_DECREF(module);
}

void test_get_app_home_exists(void) {
    PyObject *module = PyImport_ImportModule("_ralph_sys");
    TEST_ASSERT_NOT_NULL(module);

    PyObject *func = PyObject_GetAttrString(module, "get_app_home");
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "get_app_home not found");
    TEST_ASSERT_TRUE(PyCallable_Check(func));

    Py_DECREF(func);
    Py_DECREF(module);
}

void test_get_executable_path_returns_string_or_none(void) {
    PyObject *result = run_python(
        "import _ralph_sys\n"
        "p = _ralph_sys.get_executable_path()\n"
        "_sp1 = type(p).__name__\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    const char *tp = get_python_str("_sp1");
    TEST_ASSERT_NOT_NULL(tp);
    TEST_ASSERT_TRUE_MESSAGE(
        strcmp(tp, "str") == 0 || strcmp(tp, "NoneType") == 0,
        "get_executable_path should return str or None");
}

void test_get_app_home_returns_string(void) {
    PyObject *result = run_python(
        "import _ralph_sys\n"
        "h = _ralph_sys.get_app_home()\n"
        "_sp2 = type(h).__name__\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("str", get_python_str("_sp2"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sys_module_imports);
    RUN_TEST(test_get_executable_path_exists);
    RUN_TEST(test_get_app_home_exists);
    RUN_TEST(test_get_executable_path_returns_string_or_none);
    RUN_TEST(test_get_app_home_returns_string);

    if (python_initialized) {
        Py_Finalize();
        app_home_cleanup();
    }

    return UNITY_END();
}
