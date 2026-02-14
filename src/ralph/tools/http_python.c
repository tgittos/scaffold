#include "http_python.h"
#include "network/http_client.h"
#include <Python.h>
#include <stdlib.h>
#include <string.h>

/**
 * Parse a Python list of header strings into a NULL-terminated C array.
 * Returns NULL on error (with Python exception set).
 * Caller must free the returned array (but not the strings — they are
 * borrowed from the Python objects and valid for the call duration).
 */
static const char **parse_headers(PyObject *header_list, Py_ssize_t *count_out) {
    if (header_list == Py_None || header_list == NULL) {
        *count_out = 0;
        return NULL;
    }

    if (!PyList_Check(header_list)) {
        PyErr_SetString(PyExc_TypeError, "headers must be a list of strings");
        return NULL;
    }

    Py_ssize_t n = PyList_Size(header_list);
    const char **headers = malloc(sizeof(const char *) * (n + 1));
    if (headers == NULL) {
        PyErr_NoMemory();
        return NULL;
    }

    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *item = PyList_GetItem(header_list, i);
        if (!PyUnicode_Check(item)) {
            PyErr_SetString(PyExc_TypeError, "each header must be a string");
            free(headers);
            return NULL;
        }
        headers[i] = PyUnicode_AsUTF8(item);
        if (headers[i] == NULL) {
            free(headers);
            return NULL;
        }
    }
    headers[n] = NULL;
    *count_out = n;
    return headers;
}

/**
 * Build a Python dict from an HTTPResponse and C return code.
 *
 * Returns: {"status": http_status_code, "data": str, "size": int,
 *           "content_type": str, "ok": bool}
 *
 * "status" is the HTTP status code (200, 404, etc.) or 0 if the request
 * failed before getting a response (DNS failure, timeout, etc.).
 * "ok" is True when rc == 0 (request succeeded with 2xx/3xx).
 */
static PyObject *build_result_dict(int rc, const struct HTTPResponse *resp) {
    PyObject *dict = PyDict_New();
    if (dict == NULL) return NULL;

    PyObject *status = PyLong_FromLong(resp->http_status);
    if (status == NULL) { Py_DECREF(dict); return NULL; }
    PyDict_SetItemString(dict, "status", status);
    Py_DECREF(status);

    PyObject *ok = rc == 0 ? Py_True : Py_False;
    Py_INCREF(ok);
    PyDict_SetItemString(dict, "ok", ok);
    Py_DECREF(ok);

    if (resp->data != NULL && resp->size > 0) {
        PyObject *data = PyUnicode_DecodeUTF8(resp->data, (Py_ssize_t)resp->size, "replace");
        if (data == NULL) { Py_DECREF(dict); return NULL; }
        PyDict_SetItemString(dict, "data", data);
        Py_DECREF(data);
    } else {
        PyObject *empty = PyUnicode_FromString("");
        PyDict_SetItemString(dict, "data", empty);
        Py_DECREF(empty);
    }

    PyObject *size = PyLong_FromSsize_t((Py_ssize_t)resp->size);
    if (size == NULL) { Py_DECREF(dict); return NULL; }
    PyDict_SetItemString(dict, "size", size);
    Py_DECREF(size);

    if (resp->content_type != NULL) {
        PyObject *ct = PyUnicode_FromString(resp->content_type);
        if (ct == NULL) { Py_DECREF(dict); return NULL; }
        PyDict_SetItemString(dict, "content_type", ct);
        Py_DECREF(ct);
    } else {
        PyObject *empty = PyUnicode_FromString("");
        PyDict_SetItemString(dict, "content_type", empty);
        Py_DECREF(empty);
    }

    return dict;
}

/**
 * _ralph_http.get(url, headers=None, timeout=30)
 *
 * Perform an HTTP GET request using the C HTTP client.
 *
 * Returns dict with "status" (HTTP code), "ok" (bool), "data" (str),
 * "size" (int), "content_type" (str).
 */
static PyObject *py_http_get(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;

    const char *url = NULL;
    PyObject *header_list = Py_None;
    int timeout = 30;

    static char *kwlist[] = {"url", "headers", "timeout", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|Oi", kwlist,
                                     &url, &header_list, &timeout)) {
        return NULL;
    }

    if (timeout <= 0) timeout = 30;

    Py_ssize_t header_count = 0;
    const char **headers = NULL;
    if (header_list != Py_None) {
        headers = parse_headers(header_list, &header_count);
        if (headers == NULL && PyErr_Occurred()) return NULL;
    }

    struct HTTPConfig config = {
        .timeout_seconds = timeout,
        .connect_timeout_seconds = 10,
        .follow_redirects = 1,
        .max_redirects = 5
    };

    struct HTTPResponse response = {0};
    int rc = 0;

    Py_BEGIN_ALLOW_THREADS
    rc = http_get_with_config(url, headers, &config, &response);
    Py_END_ALLOW_THREADS

    free(headers);

    PyObject *result = build_result_dict(rc, &response);
    cleanup_response(&response);
    return result;
}

/**
 * _ralph_http.post(url, data, headers=None, timeout=30)
 *
 * Perform an HTTP POST request using the C HTTP client.
 *
 * Returns dict with "status" (HTTP code), "ok" (bool), "data" (str),
 * "size" (int), "content_type" (str).
 */
static PyObject *py_http_post(PyObject *self, PyObject *args, PyObject *kwargs) {
    (void)self;

    const char *url = NULL;
    const char *post_data = NULL;
    PyObject *header_list = Py_None;
    int timeout = 30;

    static char *kwlist[] = {"url", "data", "headers", "timeout", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|Oi", kwlist,
                                     &url, &post_data, &header_list, &timeout)) {
        return NULL;
    }

    if (timeout <= 0) timeout = 30;

    Py_ssize_t header_count = 0;
    const char **headers = NULL;
    if (header_list != Py_None) {
        headers = parse_headers(header_list, &header_count);
        if (headers == NULL && PyErr_Occurred()) return NULL;
    }

    struct HTTPConfig config = {
        .timeout_seconds = timeout,
        .connect_timeout_seconds = 10,
        .follow_redirects = 1,
        .max_redirects = 5
    };

    struct HTTPResponse response = {0};
    int rc = 0;

    Py_BEGIN_ALLOW_THREADS
    rc = http_post_with_config(url, post_data, headers, &config, &response);
    Py_END_ALLOW_THREADS

    free(headers);

    PyObject *result = build_result_dict(rc, &response);
    cleanup_response(&response);
    return result;
}

static PyMethodDef HttpMethods[] = {
    {"get", (PyCFunction)(void(*)(void))py_http_get, METH_VARARGS | METH_KEYWORDS,
     "HTTP GET request. get(url, headers=None, timeout=30) -> dict"},
    {"post", (PyCFunction)(void(*)(void))py_http_post, METH_VARARGS | METH_KEYWORDS,
     "HTTP POST request. post(url, data, headers=None, timeout=30) -> dict"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef http_module = {
    PyModuleDef_HEAD_INIT,
    "_ralph_http",
    "HTTP client for ralph tools (uses libcurl + mbedtls).",
    -1,
    HttpMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC PyInit__ralph_http(void) {
    return PyModule_Create(&http_module);
}

int http_python_init(void) {
    if (PyImport_AppendInittab("_ralph_http", PyInit__ralph_http) == -1) {
        return -1;
    }
    return 0;
}
