/**
 * Python Extension for HTTP Client
 *
 * Exposes the C HTTP client to Python, allowing Python-based tools
 * to make HTTP requests through libcurl + mbedtls instead of relying
 * on Python's urllib + ssl (which requires OpenSSL).
 *
 * Python Usage:
 *   import _ralph_http
 *
 *   result = _ralph_http.get("https://example.com")
 *   # result = {"status": 200, "data": "...", "size": 1234}
 *
 *   result = _ralph_http.post("https://api.example.com/data",
 *                             '{"key": "value"}',
 *                             headers=["Authorization: Bearer tok"])
 *   # result = {"status": 200, "data": "...", "size": 1234}
 */

#ifndef HTTP_PYTHON_H
#define HTTP_PYTHON_H

/**
 * Initialize the _ralph_http Python extension module.
 *
 * Must be called before Py_Initialize() to register the module.
 * The module provides:
 *   - get(url, headers=None, timeout=30) -> dict
 *   - post(url, data, headers=None, timeout=30) -> dict
 *   - download(url, dest_path, headers=None, timeout=300) -> dict
 *
 * @return 0 on success, -1 on failure
 */
int http_python_init(void);

#endif /* HTTP_PYTHON_H */
