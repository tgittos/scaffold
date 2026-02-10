#include "unity/unity.h"
#include "network/http_python.h"
#include <Python.h>
#include <string.h>
#include <stdlib.h>

static int python_initialized = 0;

void setUp(void) {
    if (!python_initialized) {
        TEST_ASSERT_EQUAL_INT(0, http_python_init());

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

void test_module_imports(void) {
    PyObject *module = PyImport_ImportModule("_ralph_http");
    TEST_ASSERT_NOT_NULL_MESSAGE(module, "Failed to import _ralph_http");
    Py_DECREF(module);
}

void test_get_function_exists(void) {
    PyObject *module = PyImport_ImportModule("_ralph_http");
    TEST_ASSERT_NOT_NULL(module);

    PyObject *func = PyObject_GetAttrString(module, "get");
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "get function not found");
    TEST_ASSERT_TRUE(PyCallable_Check(func));

    Py_DECREF(func);
    Py_DECREF(module);
}

void test_post_function_exists(void) {
    PyObject *module = PyImport_ImportModule("_ralph_http");
    TEST_ASSERT_NOT_NULL(module);

    PyObject *func = PyObject_GetAttrString(module, "post");
    TEST_ASSERT_NOT_NULL_MESSAGE(func, "post function not found");
    TEST_ASSERT_TRUE(PyCallable_Check(func));

    Py_DECREF(func);
    Py_DECREF(module);
}

void test_get_missing_url_raises_type_error(void) {
    PyObject *result = run_python(
        "import _ralph_http\n"
        "try:\n"
        "    _ralph_http.get()\n"
        "    _r1 = 'no_error'\n"
        "except TypeError:\n"
        "    _r1 = 'type_error'\n"
        "except Exception as e:\n"
        "    _r1 = str(type(e))\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("type_error", get_python_str("_r1"));
}

void test_get_bad_url_type_raises_type_error(void) {
    PyObject *result = run_python(
        "import _ralph_http\n"
        "try:\n"
        "    _ralph_http.get(12345)\n"
        "    _r2 = 'no_error'\n"
        "except TypeError:\n"
        "    _r2 = 'type_error'\n"
        "except Exception as e:\n"
        "    _r2 = str(type(e))\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("type_error", get_python_str("_r2"));
}

void test_post_missing_data_raises_type_error(void) {
    PyObject *result = run_python(
        "import _ralph_http\n"
        "try:\n"
        "    _ralph_http.post('http://example.com')\n"
        "    _r3 = 'no_error'\n"
        "except TypeError:\n"
        "    _r3 = 'type_error'\n"
        "except Exception as e:\n"
        "    _r3 = str(type(e))\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("type_error", get_python_str("_r3"));
}

void test_get_bad_headers_type_raises_type_error(void) {
    PyObject *result = run_python(
        "import _ralph_http\n"
        "try:\n"
        "    _ralph_http.get('http://example.com', headers='not-a-list')\n"
        "    _r4 = 'no_error'\n"
        "except TypeError:\n"
        "    _r4 = 'type_error'\n"
        "except Exception as e:\n"
        "    _r4 = str(type(e))\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("type_error", get_python_str("_r4"));
}

void test_get_returns_dict_with_expected_keys(void) {
    /* Use a non-routable address with short timeout to exercise the full
     * code path returning a dict without needing a live server. */
    PyObject *result = run_python(
        "import _ralph_http\n"
        "r = _ralph_http.get('http://192.0.2.1/', timeout=1)\n"
        "_r5_type = type(r).__name__\n"
        "_r5_has_status = 'status' in r\n"
        "_r5_has_ok = 'ok' in r\n"
        "_r5_has_data = 'data' in r\n"
        "_r5_has_size = 'size' in r\n"
        "_r5_has_ct = 'content_type' in r\n"
        "_r5_ok = str(r['ok'])\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("dict", get_python_str("_r5_type"));
    TEST_ASSERT_EQUAL_STRING("False", get_python_str("_r5_ok"));

    /* Verify all expected keys are present */
    PyObject *main_dict = PyModule_GetDict(PyImport_AddModule("__main__"));
    TEST_ASSERT_TRUE(PyObject_IsTrue(PyDict_GetItemString(main_dict, "_r5_has_status")));
    TEST_ASSERT_TRUE(PyObject_IsTrue(PyDict_GetItemString(main_dict, "_r5_has_ok")));
    TEST_ASSERT_TRUE(PyObject_IsTrue(PyDict_GetItemString(main_dict, "_r5_has_data")));
    TEST_ASSERT_TRUE(PyObject_IsTrue(PyDict_GetItemString(main_dict, "_r5_has_size")));
    TEST_ASSERT_TRUE(PyObject_IsTrue(PyDict_GetItemString(main_dict, "_r5_has_ct")));
}

void test_post_returns_dict(void) {
    PyObject *result = run_python(
        "import _ralph_http\n"
        "r = _ralph_http.post('http://192.0.2.1/', 'body', timeout=1)\n"
        "_r6_type = type(r).__name__\n"
        "_r6_ok = str(r['ok'])\n"
    );
    TEST_ASSERT_NOT_NULL(result);
    Py_XDECREF(result);
    TEST_ASSERT_EQUAL_STRING("dict", get_python_str("_r6_type"));
    TEST_ASSERT_EQUAL_STRING("False", get_python_str("_r6_ok"));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_module_imports);
    RUN_TEST(test_get_function_exists);
    RUN_TEST(test_post_function_exists);
    RUN_TEST(test_get_missing_url_raises_type_error);
    RUN_TEST(test_get_bad_url_type_raises_type_error);
    RUN_TEST(test_post_missing_data_raises_type_error);
    RUN_TEST(test_get_bad_headers_type_raises_type_error);
    RUN_TEST(test_get_returns_dict_with_expected_keys);
    RUN_TEST(test_post_returns_dict);

    if (python_initialized) {
        Py_Finalize();
    }

    return UNITY_END();
}
