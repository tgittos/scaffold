#include "unity.h"
#include "tools/python_tool.h"
#include "tools/python_tool_files.h"
#include "lib/tools/tools_system.h"
#include "util/app_home.h"
#include "../test_fs_utils.h"
#include <cJSON.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

static ToolRegistry registry;

void setUp(void) {
    app_home_init(NULL);
    init_tool_registry(&registry);
}

void tearDown(void) {
    cleanup_tool_registry(&registry);
    app_home_cleanup();
}

// Test that the Python interpreter initializes correctly
void test_python_interpreter_init_succeeds(void) {
    // Should already be initialized from the test main
    TEST_ASSERT_TRUE(python_interpreter_is_initialized());
}

// Test tool registration and execution through the tool registry
void test_python_tool_through_registry(void) {
    TEST_ASSERT_EQUAL_INT(0, register_python_tool(&registry));
    TEST_ASSERT_EQUAL_INT(1, registry.functions.count);
    TEST_ASSERT_EQUAL_STRING("python", registry.functions.data[0].name);

    ToolCall call = {
        .id = "registry-test-1",
        .name = "python",
        .arguments = "{\"code\": \"print(2 + 2)\"}"
    };
    ToolResult result = {0};

    TEST_ASSERT_EQUAL_INT(0, execute_python_tool_call(&call, &result));
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "\"stdout\":\"4"));

    free(result.tool_call_id);
    free(result.result);
}

// Test basic code execution
void test_python_execute_basic_code(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("print('Hello from Python!')");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "Hello from Python!"));
    TEST_ASSERT_EQUAL_INT(0, result.timed_out);

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test state persistence across calls
void test_python_state_persists(void) {
    // First call: define a variable
    PythonExecutionParams params1 = {0};
    params1.code = strdup("my_persistent_var = 42");
    params1.timeout_seconds = 30;

    PythonExecutionResult result1 = {0};
    int ret1 = execute_python_code(&params1, &result1);

    TEST_ASSERT_EQUAL_INT(0, ret1);
    TEST_ASSERT_EQUAL_INT(1, result1.success);

    cleanup_python_params(&params1);
    cleanup_python_result(&result1);

    // Second call: use the variable
    PythonExecutionParams params2 = {0};
    params2.code = strdup("print(my_persistent_var * 2)");
    params2.timeout_seconds = 30;

    PythonExecutionResult result2 = {0};
    int ret2 = execute_python_code(&params2, &result2);

    TEST_ASSERT_EQUAL_INT(0, ret2);
    TEST_ASSERT_EQUAL_INT(1, result2.success);
    TEST_ASSERT_NOT_NULL(result2.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result2.stdout_output, "84"));

    cleanup_python_params(&params2);
    cleanup_python_result(&result2);
}

// Test function persistence
void test_python_function_persists(void) {
    // First call: define a function
    PythonExecutionParams params1 = {0};
    params1.code = strdup("def double(x):\n    return x * 2");
    params1.timeout_seconds = 30;

    PythonExecutionResult result1 = {0};
    int ret1 = execute_python_code(&params1, &result1);

    TEST_ASSERT_EQUAL_INT(0, ret1);
    TEST_ASSERT_EQUAL_INT(1, result1.success);

    cleanup_python_params(&params1);
    cleanup_python_result(&result1);

    // Second call: use the function
    PythonExecutionParams params2 = {0};
    params2.code = strdup("print(double(21))");
    params2.timeout_seconds = 30;

    PythonExecutionResult result2 = {0};
    int ret2 = execute_python_code(&params2, &result2);

    TEST_ASSERT_EQUAL_INT(0, ret2);
    TEST_ASSERT_EQUAL_INT(1, result2.success);
    TEST_ASSERT_NOT_NULL(result2.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result2.stdout_output, "42"));

    cleanup_python_params(&params2);
    cleanup_python_result(&result2);
}

// Test exception handling
void test_python_exception_handling(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("raise ValueError('Test exception message')");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_NOT_NULL(result.exception);
    TEST_ASSERT_NOT_NULL(strstr(result.exception, "ValueError"));
    TEST_ASSERT_NOT_NULL(strstr(result.exception, "Test exception message"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that interpreter state is preserved after exception
void test_python_state_after_exception(void) {
    // Define a variable
    PythonExecutionParams params1 = {0};
    params1.code = strdup("recovery_var = 123");
    params1.timeout_seconds = 30;

    PythonExecutionResult result1 = {0};
    execute_python_code(&params1, &result1);
    cleanup_python_params(&params1);
    cleanup_python_result(&result1);

    // Cause an exception
    PythonExecutionParams params2 = {0};
    params2.code = strdup("1/0");
    params2.timeout_seconds = 30;

    PythonExecutionResult result2 = {0};
    execute_python_code(&params2, &result2);
    TEST_ASSERT_EQUAL_INT(0, result2.success);
    cleanup_python_params(&params2);
    cleanup_python_result(&result2);

    // Variable should still exist
    PythonExecutionParams params3 = {0};
    params3.code = strdup("print(recovery_var)");
    params3.timeout_seconds = 30;

    PythonExecutionResult result3 = {0};
    execute_python_code(&params3, &result3);

    TEST_ASSERT_EQUAL_INT(1, result3.success);
    TEST_ASSERT_NOT_NULL(result3.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result3.stdout_output, "123"));

    cleanup_python_params(&params3);
    cleanup_python_result(&result3);
}

// Test timeout (with short timeout)
void test_python_timeout(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("import time; time.sleep(10)");
    params.timeout_seconds = 1;  // 1 second timeout

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(0, result.success);
    TEST_ASSERT_EQUAL_INT(1, result.timed_out);

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test stdlib import (json module)
void test_python_stdlib_json(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("import json; print(json.dumps({'key': 'value'}))");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success, "json import should succeed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "key"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test stdlib import (math module)
void test_python_stdlib_math(void) {
    PythonExecutionParams params = {0};
    params.code = strdup("import math; print(int(math.sqrt(144)))");
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "12"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that shell tool exit_code propagates to result->success
void test_shell_tool_exit_code_propagates(void) {
    // Initialize tool files
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    // Create a registry and register tools
    ToolRegistry shell_registry;
    init_tool_registry(&shell_registry);
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&shell_registry));

    // Test 1: Command that exits 0 should have success=1
    ToolCall call_success = {
        .id = "shell-exit-0",
        .name = "shell",
        .arguments = "{\"command\": \"true\"}"
    };
    ToolResult result_success = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call_success, &result_success));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result_success.success,
        "Shell command 'true' (exit 0) should report success=1");
    TEST_ASSERT_NOT_NULL(result_success.result);
    TEST_ASSERT_NOT_NULL(strstr(result_success.result, "\"exit_code\": 0"));
    free(result_success.tool_call_id);
    free(result_success.result);

    // Test 2: Command that exits non-zero should have success=0
    ToolCall call_fail = {
        .id = "shell-exit-1",
        .name = "shell",
        .arguments = "{\"command\": \"false\"}"
    };
    ToolResult result_fail = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call_fail, &result_fail));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result_fail.success,
        "Shell command 'false' (exit 1) should report success=0");
    TEST_ASSERT_NOT_NULL(result_fail.result);
    TEST_ASSERT_NOT_NULL(strstr(result_fail.result, "\"exit_code\": 1"));
    free(result_fail.tool_call_id);
    free(result_fail.result);

    // Test 3: Command with specific exit code
    ToolCall call_exit42 = {
        .id = "shell-exit-42",
        .name = "shell",
        .arguments = "{\"command\": \"exit 42\"}"
    };
    ToolResult result_exit42 = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call_exit42, &result_exit42));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, result_exit42.success,
        "Shell command 'exit 42' should report success=0");
    TEST_ASSERT_NOT_NULL(result_exit42.result);
    TEST_ASSERT_NOT_NULL(strstr(result_exit42.result, "\"exit_code\": 42"));
    free(result_exit42.tool_call_id);
    free(result_exit42.result);

    cleanup_tool_registry(&shell_registry);
    python_cleanup_tool_files();
}

// Test that docstring Args section is parsed correctly for tool descriptions
void test_python_tool_docstring_parsing(void) {
    // Initialize tool files
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());

    // Load Python tool files
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    // Create a registry and register tools
    ToolRegistry file_registry;
    init_tool_registry(&file_registry);
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&file_registry));

    // Find the apply_patch tool
    int found_apply_patch = 0;
    for (size_t i = 0; i < file_registry.functions.count; i++) {
        if (strcmp(file_registry.functions.data[i].name, "apply_patch") == 0) {
            found_apply_patch = 1;

            // Check that 'patch' parameter exists with a real description
            for (int j = 0; j < file_registry.functions.data[i].parameter_count; j++) {
                if (strcmp(file_registry.functions.data[i].parameters[j].name, "patch") == 0) {
                    const char *desc = file_registry.functions.data[i].parameters[j].description;
                    TEST_ASSERT_NOT_NULL(desc);
                    TEST_ASSERT_NOT_NULL_MESSAGE(
                        strstr(desc, "patch"),
                        "patch description should mention 'patch'"
                    );
                    break;
                }
            }
            break;
        }
    }

    TEST_ASSERT_TRUE_MESSAGE(found_apply_patch, "apply_patch tool should be registered");

    cleanup_tool_registry(&file_registry);
    python_cleanup_tool_files();
}

// Test that _ralph_sys module is importable and returns correct types
void test_sys_module_accessible(void) {
    PythonExecutionParams params = {0};
    params.code = strdup(
        "import _ralph_sys\n"
        "h = _ralph_sys.get_app_home()\n"
        "assert isinstance(h, str), f'expected str, got {type(h)}'\n"
        "p = _ralph_sys.get_executable_path()\n"
        "assert p is None or isinstance(p, str)\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "sys module test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that _version_key orders stable releases above pre-releases
void test_pip_version_key_ordering(void) {
    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/pip.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_vk = _ns['_version_key']\n"
        "# Stable > pre-release\n"
        "assert _vk('1.0.0') > _vk('1.0.0rc1'), '1.0.0 > 1.0.0rc1'\n"
        "assert _vk('1.0.0') > _vk('1.0.0b1'), '1.0.0 > 1.0.0b1'\n"
        "assert _vk('1.0.0') > _vk('1.0.0a1'), '1.0.0 > 1.0.0a1'\n"
        "assert _vk('1.0.0') > _vk('1.0.0dev1'), '1.0.0 > 1.0.0dev1'\n"
        "# rc > beta > alpha > dev\n"
        "assert _vk('1.0.0rc1') > _vk('1.0.0b1'), 'rc > beta'\n"
        "assert _vk('1.0.0b1') > _vk('1.0.0a1'), 'beta > alpha'\n"
        "assert _vk('1.0.0a1') > _vk('1.0.0dev1'), 'alpha > dev'\n"
        "# Higher version > lower\n"
        "assert _vk('2.0.0') > _vk('1.9.9'), '2.0.0 > 1.9.9'\n"
        "assert _vk('1.1.0') > _vk('1.0.9'), '1.1.0 > 1.0.9'\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "version_key ordering failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that _find_best_wheel selects py3-none-any and rejects native wheels
void test_pip_find_best_wheel(void) {
    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/pip.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_fbw = _ns['_find_best_wheel']\n"
        "\n"
        "# Synthetic PyPI simple HTML for six\n"
        "six_html = (\n"
        "    '<a href=\"https://files.pythonhosted.org/six-1.16.0-py2.py3-none-any.whl#sha256=abc\">six-1.16.0-py2.py3-none-any.whl</a>\\n'\n"
        "    '<a href=\"https://files.pythonhosted.org/six-1.15.0-py2.py3-none-any.whl#sha256=def\">six-1.15.0-py2.py3-none-any.whl</a>\\n'\n"
        ")\n"
        "\n"
        "# Should pick latest pure-python wheel\n"
        "url, fname = _fbw(six_html, 'six', None)\n"
        "assert url is not None, 'should find a wheel'\n"
        "assert 'six-1.16.0' in fname, f'expected 1.16.0, got {fname}'\n"
        "assert '#' not in url, 'URL should have fragment stripped'\n"
        "\n"
        "# With specific version\n"
        "url2, fname2 = _fbw(six_html, 'six', '1.15.0')\n"
        "assert '1.15.0' in fname2, f'expected 1.15.0, got {fname2}'\n"
        "\n"
        "# PyPI page for numpy: only native wheels, no py3-none-any\n"
        "numpy_html = (\n"
        "    '<a href=\"https://files.pythonhosted.org/numpy-1.26.0-cp312-cp312-linux_x86_64.whl#sha256=ghi\">numpy-1.26.0-cp312-cp312-linux_x86_64.whl</a>\\n'\n"
        "    '<a href=\"https://files.pythonhosted.org/numpy-1.26.0-cp312-cp312-macosx_14_0_arm64.whl#sha256=jkl\">numpy-1.26.0-cp312-cp312-macosx_14_0_arm64.whl</a>\\n'\n"
        ")\n"
        "url3, _ = _fbw(numpy_html, 'numpy', None)\n"
        "assert url3 is None, 'numpy should have no compatible wheel'\n"
        "\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "find_best_wheel test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that _safe_extractall rejects zip entries with path traversal
void test_pip_safe_extractall_rejects_traversal(void) {
    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/pip.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_safe = _ns['_safe_extractall']\n"
        "\n"
        "import zipfile, io, tempfile, os\n"
        "\n"
        "# Create a zip with a path-traversal entry\n"
        "buf = io.BytesIO()\n"
        "with zipfile.ZipFile(buf, 'w') as zf:\n"
        "    zf.writestr('../../../tmp/evil.txt', 'malicious')\n"
        "buf.seek(0)\n"
        "\n"
        "with tempfile.TemporaryDirectory() as tmpdir:\n"
        "    with zipfile.ZipFile(buf, 'r') as zf:\n"
        "        errors = []\n"
        "        ok = _safe(zf, tmpdir, errors)\n"
        "        assert ok == False, f'expected False, got {ok}'\n"
        "        assert len(errors) > 0, 'should have error'\n"
        "        assert 'Unsafe path' in errors[0], f'unexpected error: {errors[0]}'\n"
        "\n"
        "# Create a zip with safe entries\n"
        "buf2 = io.BytesIO()\n"
        "with zipfile.ZipFile(buf2, 'w') as zf:\n"
        "    zf.writestr('pkg/module.py', 'x = 1')\n"
        "buf2.seek(0)\n"
        "\n"
        "with tempfile.TemporaryDirectory() as tmpdir:\n"
        "    with zipfile.ZipFile(buf2, 'r') as zf:\n"
        "        errors = []\n"
        "        ok = _safe(zf, tmpdir, errors)\n"
        "        assert ok == True, f'expected True, got {ok}'\n"
        "        assert len(errors) == 0, f'unexpected errors: {errors}'\n"
        "        assert os.path.exists(os.path.join(tmpdir, 'pkg', 'module.py'))\n"
        "\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "safe_extractall test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test pip_list with an empty site-packages directory
void test_pip_list_empty_site_packages(void) {
    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/pip.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_pip = _ns['pip']\n"
        "\n"
        "import tempfile, os\n"
        "# Mock _ralph_sys to return a temp dir as app home\n"
        "d = tempfile.mkdtemp()\n"
        "sp = os.path.join(d, 'site-packages')\n"
        "os.makedirs(sp)\n"
        "\n"
        "import types\n"
        "mock_sys = types.ModuleType('_ralph_sys')\n"
        "mock_sys.get_app_home = lambda: d\n"
        "import sys\n"
        "sys.modules['_ralph_sys'] = mock_sys\n"
        "\n"
        "result = _pip(action='list')\n"
        "assert result['count'] == 0, f'expected 0, got {result[\"count\"]}'\n"
        "assert result['packages'] == [], f'expected [], got {result[\"packages\"]}'\n"
        "\n"
        "# Clean up\n"
        "os.rmdir(sp)\n"
        "os.rmdir(d)\n"
        "del sys.modules['_ralph_sys']\n"
        "\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "pip_list empty test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
}

// Test that read_file returns a dict with content, total_lines, and range
void test_read_file_returns_dict(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/read_file.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_read_file = _ns['read_file']\n"
        "\n"
        "import tempfile, os\n"
        "tf = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "tf.write('alpha\\nbeta\\ngamma\\ndelta\\n')\n"
        "tf.close()\n"
        "\n"
        "# Full file read\n"
        "r = _read_file(tf.name)\n"
        "assert isinstance(r, dict), f'expected dict, got {type(r)}'\n"
        "assert 'content' in r, 'missing content key'\n"
        "assert 'total_lines' in r, 'missing total_lines key'\n"
        "assert 'range' in r, 'missing range key'\n"
        "assert r['total_lines'] == 4, f'expected 4 lines, got {r[\"total_lines\"]}'\n"
        "assert r['range'] == '1-4', f'expected 1-4, got {r[\"range\"]}'\n"
        "assert r['success'] == True, 'expected success=True'\n"
        "assert '1: alpha' in r['content'], f'missing line numbers in content: {r[\"content\"][:100]}'\n"
        "assert '3: gamma' in r['content'], f'missing line 3: {r[\"content\"][:200]}'\n"
        "\n"
        "# Ranged read\n"
        "r2 = _read_file(tf.name, start_line=2, end_line=3)\n"
        "assert r2['total_lines'] == 4, 'total_lines should be full file count'\n"
        "assert r2['range'] == '2-3', f'expected 2-3, got {r2[\"range\"]}'\n"
        "assert '2: beta' in r2['content']\n"
        "assert '1: alpha' not in r2['content']\n"
        "\n"
        "# Empty file\n"
        "ef = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "ef.close()\n"
        "r3 = _read_file(ef.name)\n"
        "assert r3['total_lines'] == 0, f'expected 0, got {r3[\"total_lines\"]}'\n"
        "assert r3['range'] == '0-0', f'expected 0-0, got {r3[\"range\"]}'\n"
        "os.unlink(ef.name)\n"
        "\n"
        "os.unlink(tf.name)\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "read_file dict test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
    python_cleanup_tool_files();
}

// Test search_files new features: context_lines, matched_files, total_matches_found
void test_search_files_new_features(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/search_files.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_search = _ns['search_files']\n"
        "\n"
        "import tempfile, os\n"
        "td = tempfile.mkdtemp()\n"
        "fpath = os.path.join(td, 'test.txt')\n"
        "with open(fpath, 'w') as f:\n"
        "    f.write('line1 foo\\nline2 bar\\nline3 foo\\nline4 baz\\nline5 foo\\n')\n"
        "\n"
        "# Basic search\n"
        "r = _search(td, 'foo')\n"
        "assert 'total_matches_found' in r, 'missing total_matches_found'\n"
        "assert 'matched_files' in r, 'missing matched_files'\n"
        "assert r['total_matches_found'] == 3, f'expected 3 total, got {r[\"total_matches_found\"]}'\n"
        "assert len(r['matched_files']) == 1, f'expected 1 file, got {len(r[\"matched_files\"])}'\n"
        "assert fpath in r['matched_files'][0] or r['matched_files'][0].endswith('test.txt')\n"
        "\n"
        "# With context_lines\n"
        "r2 = _search(td, 'bar', context_lines=1)\n"
        "assert len(r2['results']) == 1\n"
        "m = r2['results'][0]\n"
        "assert 'surrounding_lines' in m, 'missing surrounding_lines'\n"
        "sl = m['surrounding_lines']\n"
        "assert len(sl) == 2, f'expected 2 surrounding lines, got {len(sl)}'\n"
        "# Line 2 matches 'bar', so context should include lines 1 and 3\n"
        "line_nums = [s['line_number'] for s in sl]\n"
        "assert 1 in line_nums and 3 in line_nums, f'unexpected line nums: {line_nums}'\n"
        "\n"
        "# Without context_lines, no surrounding_lines key\n"
        "r3 = _search(td, 'bar', context_lines=0)\n"
        "assert 'surrounding_lines' not in r3['results'][0]\n"
        "\n"
        "# total_matches_found with max_results\n"
        "r4 = _search(td, 'foo', max_results=1)\n"
        "assert r4['total_matches'] == 1, 'should cap results at 1'\n"
        "assert r4['total_matches_found'] >= 1, f'should count at least 1, got {r4[\"total_matches_found\"]}'\n"
        "assert r4['truncated'] == True\n"
        "\n"
        "# finditer: multiple matches on same line\n"
        "fpath2 = os.path.join(td, 'multi.txt')\n"
        "with open(fpath2, 'w') as f:\n"
        "    f.write('aaa bbb aaa\\n')\n"
        "r5 = _search(td, 'aaa', glob_filter='multi.txt')\n"
        "assert r5['total_matches_found'] == 2, f'finditer should find 2, got {r5[\"total_matches_found\"]}'\n"
        "\n"
        "# Clean up\n"
        "os.unlink(fpath)\n"
        "os.unlink(fpath2)\n"
        "os.rmdir(td)\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "search_files features test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
    python_cleanup_tool_files();
}

// Test list_dir ISO timestamps
void test_list_dir_iso_timestamps(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/list_dir.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_list_dir = _ns['list_dir']\n"
        "\n"
        "import tempfile, os\n"
        "td = tempfile.mkdtemp()\n"
        "fpath = os.path.join(td, 'hello.txt')\n"
        "with open(fpath, 'w') as f:\n"
        "    f.write('hi')\n"
        "\n"
        "r = _list_dir(td)\n"
        "assert len(r) == 1\n"
        "entry = r[0]\n"
        "mt = entry['modified_time']\n"
        "assert isinstance(mt, str), f'expected str, got {type(mt)}'\n"
        "# Should be ISO format with T separator\n"
        "assert 'T' in mt, f'expected ISO format, got {mt}'\n"
        "# Should parse as datetime\n"
        "from datetime import datetime\n"
        "datetime.fromisoformat(mt)\n"
        "\n"
        "os.unlink(fpath)\n"
        "os.rmdir(td)\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "list_dir ISO timestamp test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
    python_cleanup_tool_files();
}

// Test apply_patch basic operations
void test_apply_patch_basic(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    PythonExecutionParams params = {0};
    params.code = strdup(
        "_ns = {}\n"
        "with open('/zip/python_defaults/apply_patch.py', 'r') as f:\n"
        "    exec(f.read(), _ns)\n"
        "_apply_patch = _ns['apply_patch']\n"
        "\n"
        "import tempfile, os\n"
        "\n"
        "# --- Test 1: Basic single-hunk update (replace line) ---\n"
        "tf = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "tf.write('line1\\nline2\\nline3\\n')\n"
        "tf.close()\n"
        "\n"
        "patch = ('*** Begin Patch\\n'\n"
        "         '*** Update File: ' + tf.name + '\\n'\n"
        "         '@@ line1\\n'\n"
        "         ' line2\\n'\n"
        "         '-line3\\n'\n"
        "         '+line3_replaced\\n'\n"
        "         '*** End Patch')\n"
        "r = _apply_patch(patch)\n"
        "assert r['success'] == True, f'basic update failed: {r}'\n"
        "with open(tf.name) as f:\n"
        "    c = f.read()\n"
        "assert 'line3_replaced' in c, f'replacement not found: {c}'\n"
        "assert 'line1' in c, 'line1 should be preserved'\n"
        "assert 'line2' in c, 'line2 should be preserved'\n"
        "\n"
        "# --- Test 2: Multi-hunk update on same file ---\n"
        "tf2 = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "tf2.write('aaa\\nbbb\\nccc\\nddd\\neee\\n')\n"
        "tf2.close()\n"
        "\n"
        "patch2 = ('*** Begin Patch\\n'\n"
        "          '*** Update File: ' + tf2.name + '\\n'\n"
        "          '@@ aaa\\n'\n"
        "          '-bbb\\n'\n"
        "          '+BBB\\n'\n"
        "          '@@ ddd\\n'\n"
        "          '-eee\\n'\n"
        "          '+EEE\\n'\n"
        "          '*** End Patch')\n"
        "r2 = _apply_patch(patch2)\n"
        "assert r2['success'] == True\n"
        "with open(tf2.name) as f:\n"
        "    c2 = f.read()\n"
        "assert 'BBB' in c2 and 'EEE' in c2, f'multi-hunk failed: {c2}'\n"
        "\n"
        "# --- Test 3: Add new file ---\n"
        "new_file = tf.name + '.new'\n"
        "if os.path.exists(new_file):\n"
        "    os.unlink(new_file)\n"
        "patch3 = ('*** Begin Patch\\n'\n"
        "          '*** Add File: ' + new_file + '\\n'\n"
        "          '+new line 1\\n'\n"
        "          '+new line 2\\n'\n"
        "          '*** End Patch')\n"
        "r3 = _apply_patch(patch3)\n"
        "assert r3['success'] == True\n"
        "assert os.path.exists(new_file), 'new file should exist'\n"
        "with open(new_file) as f:\n"
        "    c3 = f.read()\n"
        "assert 'new line 1' in c3 and 'new line 2' in c3\n"
        "\n"
        "# --- Test 4: Delete file ---\n"
        "patch4 = ('*** Begin Patch\\n'\n"
        "          '*** Delete File: ' + new_file + '\\n'\n"
        "          '*** End Patch')\n"
        "r4 = _apply_patch(patch4)\n"
        "assert r4['success'] == True\n"
        "assert not os.path.exists(new_file), 'file should be deleted'\n"
        "\n"
        "# --- Test 5: Context mismatch error ---\n"
        "tf5 = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "tf5.write('alpha\\nbeta\\ngamma\\n')\n"
        "tf5.close()\n"
        "patch5 = ('*** Begin Patch\\n'\n"
        "          '*** Update File: ' + tf5.name + '\\n'\n"
        "          '@@ nonexistent_anchor\\n'\n"
        "          '-beta\\n'\n"
        "          '+BETA\\n'\n"
        "          '*** End Patch')\n"
        "try:\n"
        "    _apply_patch(patch5)\n"
        "    assert False, 'should have raised ValueError'\n"
        "except ValueError as e:\n"
        "    assert 'Anchor not found' in str(e), f'unexpected error: {e}'\n"
        "\n"
        "# --- Test 6: Multi-file patch ---\n"
        "tf6a = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "tf6a.write('file_a_line1\\nfile_a_line2\\n')\n"
        "tf6a.close()\n"
        "tf6b = tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False)\n"
        "tf6b.write('file_b_line1\\nfile_b_line2\\n')\n"
        "tf6b.close()\n"
        "patch6 = ('*** Begin Patch\\n'\n"
        "          '*** Update File: ' + tf6a.name + '\\n'\n"
        "          '-file_a_line1\\n'\n"
        "          '+FILE_A_LINE1\\n'\n"
        "          '*** Update File: ' + tf6b.name + '\\n'\n"
        "          '-file_b_line1\\n'\n"
        "          '+FILE_B_LINE1\\n'\n"
        "          '*** End Patch')\n"
        "r6 = _apply_patch(patch6)\n"
        "assert r6['success'] == True\n"
        "assert len(r6['files_modified']) == 2\n"
        "with open(tf6a.name) as f:\n"
        "    assert 'FILE_A_LINE1' in f.read()\n"
        "with open(tf6b.name) as f:\n"
        "    assert 'FILE_B_LINE1' in f.read()\n"
        "\n"
        "# Cleanup\n"
        "for f in [tf.name, tf2.name, tf5.name, tf6a.name, tf6b.name]:\n"
        "    if os.path.exists(f):\n"
        "        os.unlink(f)\n"
        "\n"
        "print('passed')\n"
    );
    params.timeout_seconds = PYTHON_DEFAULT_TIMEOUT;

    PythonExecutionResult result = {0};
    int ret = execute_python_code(&params, &result);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success,
        result.exception ? result.exception : "apply_patch test failed");
    TEST_ASSERT_NOT_NULL(result.stdout_output);
    TEST_ASSERT_NOT_NULL(strstr(result.stdout_output, "passed"));

    cleanup_python_params(&params);
    cleanup_python_result(&result);
    python_cleanup_tool_files();
}

static const char *SHIPPED_APPEND_FILE_PY =
    "\"\"\"Append content to a file.\"\"\"\n"
    "\n"
    "def append_file(path: str, content: str) -> dict:\n"
    "    \"\"\"Append content to file.\n"
    "\n"
    "    Args:\n"
    "        path: Path to the file to append to\n"
    "        content: Content to append\n"
    "\n"
    "    Returns:\n"
    "        Dictionary with success status and path\n"
    "    \"\"\"\n"
    "    from pathlib import Path\n"
    "\n"
    "    p = Path(path).resolve()\n"
    "\n"
    "    # Security check - prevent directory traversal\n"
    "    if '..' in str(path):\n"
    "        raise ValueError(\"Invalid path: directory traversal not allowed\")\n"
    "\n"
    "    # Create parent directories if needed\n"
    "    p.parent.mkdir(parents=True, exist_ok=True)\n"
    "\n"
    "    # Append content\n"
    "    with open(p, 'a', encoding='utf-8') as f:\n"
    "        f.write(content)\n"
    "\n"
    "    return {\n"
    "        \"success\": True,\n"
    "        \"path\": str(p),\n"
    "        \"bytes_appended\": len(content.encode('utf-8'))\n"
    "    }\n";

static void write_text_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(content, f);
    fclose(f);
}

void test_retired_default_tools_removed_only_when_unmodified(void) {
    char home[] = "/tmp/retired_tools_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(home));
    app_home_cleanup();
    TEST_ASSERT_EQUAL_INT(0, app_home_init(home));

    char tools_dir[512], shipped[600], customized[600], user_tool[600];
    snprintf(tools_dir, sizeof(tools_dir), "%s/tools", home);
    TEST_ASSERT_EQUAL_INT(0, mkdir(tools_dir, 0755));
    snprintf(shipped, sizeof(shipped), "%s/append_file.py", tools_dir);
    snprintf(customized, sizeof(customized), "%s/pip_list.py", tools_dir);
    snprintf(user_tool, sizeof(user_tool), "%s/my_tool.py", tools_dir);
    write_text_file(shipped, SHIPPED_APPEND_FILE_PY);
    write_text_file(customized, "def pip_list() -> dict:\n    return {}\n");
    write_text_file(user_tool, "def my_tool() -> dict:\n    return {}\n");

    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());

    struct stat st;
    TEST_ASSERT_NOT_EQUAL(0, stat(shipped, &st));
    TEST_ASSERT_EQUAL_INT(0, stat(customized, &st));
    TEST_ASSERT_EQUAL_INT(0, stat(user_tool, &st));

    python_cleanup_tool_files();
    app_home_cleanup();
    rmdir_recursive(home);
}

static volatile int file_tool_thread_done = 0;
static ToolResult file_tool_thread_result;

static void *run_file_tool_call(void *arg) {
    execute_python_file_tool_call((const ToolCall *)arg, &file_tool_thread_result);
    file_tool_thread_done = 1;
    return NULL;
}

void test_file_tool_runs_on_non_main_thread(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    char dir[] = "/tmp/test_gil_thread_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(dir));
    char args[128];
    snprintf(args, sizeof(args), "{\"path\": \"%s\"}", dir);
    ToolCall call = { .id = "thread-list-dir", .name = "list_dir", .arguments = args };

    memset(&file_tool_thread_result, 0, sizeof(file_tool_thread_result));
    file_tool_thread_done = 0;
    pthread_t thread;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(&thread, NULL, run_file_tool_call, &call));

    for (int i = 0; i < 500 && !file_tool_thread_done; i++) {
        usleep(10000);
    }
    if (!file_tool_thread_done) {
        /* The blocked thread cannot be joined, so the process cannot continue. */
        printf("%s:%d:%s:FAIL: deadlock: GIL held by init thread\n", __FILE__, __LINE__, __func__);
        fflush(stdout);
        _exit(1);
    }
    TEST_ASSERT_TRUE_MESSAGE(file_tool_thread_done, "deadlock: GIL held by init thread");
    pthread_join(thread, NULL);

    TEST_ASSERT_EQUAL_INT(1, file_tool_thread_result.success);

    free(file_tool_thread_result.tool_call_id);
    free(file_tool_thread_result.result);
    rmdir(dir);
    python_cleanup_tool_files();
}

void test_interpreter_restart_after_gil_release(void) {
    python_interpreter_shutdown();
    TEST_ASSERT_EQUAL_INT(0, python_interpreter_init());

    ToolCall call = { .id = "restart-1", .name = "python", .arguments = "{\"code\": \"print('ok')\"}" };
    ToolResult result = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_tool_call(&call, &result));
    TEST_ASSERT_EQUAL_INT(1, result.success);
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_NOT_NULL(strstr(result.result, "ok"));

    free(result.tool_call_id);
    free(result.result);
}

#define ISOLATION_THREADS 8
#define ISOLATION_ITERATIONS 50

typedef struct {
    int index;
    int parties;
    char token[32];
    int expect_error;
    int mismatches;
    int failures;
} IsolationWorker;

static IsolationWorker isolation_workers[ISOLATION_THREADS];

static int contains_other_token(const char *text, int self) {
    for (int i = 0; i < ISOLATION_THREADS; i++) {
        if (i != self && isolation_workers[i].token[0] != '\0'
            && strstr(text, isolation_workers[i].token) != NULL) {
            return 1;
        }
    }
    return 0;
}

static void *run_isolation_worker(void *arg) {
    IsolationWorker *w = (IsolationWorker *)arg;
    char args[128];
    snprintf(args, sizeof(args), "{\"token\": \"%s\", \"parties\": %d}", w->token, w->parties);
    for (int i = 0; i < ISOLATION_ITERATIONS; i++) {
        ToolCall call = { .id = "isolation", .name = "isolation_echo", .arguments = args };
        ToolResult result = {0};
        execute_python_file_tool_call(&call, &result);
        const char *text = result.result ? result.result : "";
        if (w->expect_error) {
            if (result.success != 0 || strstr(text, "\"error\"") == NULL) w->failures++;
        } else {
            if (result.success != 1) w->failures++;
            if (strstr(text, w->token) == NULL) w->mismatches++;
        }
        if (contains_other_token(text, w->index)) w->mismatches++;
        free(result.tool_call_id);
        free(result.result);
    }
    return NULL;
}

/* The returned object's __class__ property waits on a barrier shared by all
 * successful callers. isinstance() reads __class__, so every call is held
 * while its result is being converted until all of them have produced theirs. */
static void load_isolation_tool(char *path, size_t path_size) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    snprintf(path, path_size, "%s/isolation_echo.py", python_get_tools_dir());
    write_text_file(path,
        "import threading\n"
        "\n"
        "_isolation_barriers = {}\n"
        "_isolation_lock = threading.Lock()\n"
        "\n"
        "class _IsolationEcho:\n"
        "    def __init__(self, token, barrier):\n"
        "        self.token = token\n"
        "        self.barrier = barrier\n"
        "\n"
        "    @property\n"
        "    def __class__(self):\n"
        "        self.barrier.wait(timeout=10)\n"
        "        return _IsolationEcho\n"
        "\n"
        "    def __str__(self):\n"
        "        return self.token\n"
        "\n"
        "def isolation_echo(token: str, parties: int):\n"
        "    \"\"\"Echo token, raising when it is empty.\n"
        "\n"
        "    Args:\n"
        "        token: Token to echo\n"
        "        parties: Number of concurrent successful callers\n"
        "    \"\"\"\n"
        "    if not token:\n"
        "        raise ValueError('empty token')\n"
        "    with _isolation_lock:\n"
        "        barrier = _isolation_barriers.setdefault(parties, threading.Barrier(parties))\n"
        "    return _IsolationEcho(token, barrier)\n");
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());
}

static void setup_isolation_workers(int count, int error_index) {
    memset(isolation_workers, 0, sizeof(isolation_workers));
    for (int i = 0; i < count; i++) {
        isolation_workers[i].index = i;
        isolation_workers[i].parties = error_index < 0 ? count : count - 1;
        if (i == error_index) {
            isolation_workers[i].expect_error = 1;
        } else {
            snprintf(isolation_workers[i].token, sizeof(isolation_workers[i].token),
                     "TOKEN_%d_%d_X", (int)getpid(), i);
        }
    }
}

static void run_isolation_workers(int count) {
    pthread_t threads[ISOLATION_THREADS];
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[i], NULL, run_isolation_worker, &isolation_workers[i]));
    }
    for (int i = 0; i < count; i++) {
        pthread_join(threads[i], NULL);
    }
}

static void assert_isolation_workers(int count) {
    for (int i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, isolation_workers[i].mismatches, "result carried another call's token");
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, isolation_workers[i].failures, "success flag did not match the call's own outcome");
    }
}

static void finish_isolation_test(const char *tool_path, int count) {
    unlink(tool_path);
    python_cleanup_tool_files();
    assert_isolation_workers(count);
}

void test_concurrent_calls_isolate_results(void) {
    char tool_path[512];
    load_isolation_tool(tool_path, sizeof(tool_path));
    setup_isolation_workers(ISOLATION_THREADS, -1);
    run_isolation_workers(ISOLATION_THREADS);
    finish_isolation_test(tool_path, ISOLATION_THREADS);
}

void test_concurrent_calls_isolate_exceptions(void) {
    char tool_path[512];
    load_isolation_tool(tool_path, sizeof(tool_path));
    setup_isolation_workers(5, 4);
    run_isolation_workers(5);
    finish_isolation_test(tool_path, 5);
}

void test_invoke_nested_json_literals(void) {
    char home[] = "/tmp/invoke_literals_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(home));
    app_home_cleanup();
    TEST_ASSERT_EQUAL_INT(0, app_home_init(home));

    char tools_dir[512], tool_path[600];
    snprintf(tools_dir, sizeof(tools_dir), "%s/tools", home);
    TEST_ASSERT_EQUAL_INT(0, mkdir(tools_dir, 0755));
    snprintf(tool_path, sizeof(tool_path), "%s/echo_args.py", tools_dir);
    write_text_file(tool_path,
        "def echo_args(payload: dict) -> dict:\n"
        "    \"\"\"Echo the payload back.\n"
        "\n"
        "    Args:\n"
        "        payload: Arbitrary object\n"
        "    \"\"\"\n"
        "    return {\"payload\": payload}\n");

    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    ToolCall call = { .id = "echo-1", .name = "echo_args",
                      .arguments = "{\"payload\": {\"a\": true, \"b\": null}}" };
    ToolResult result = {0};
    TEST_ASSERT_EQUAL_INT(0, execute_python_file_tool_call(&call, &result));
    TEST_ASSERT_NOT_NULL(result.result);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, result.success, result.result);

    cJSON *json = cJSON_Parse(result.result);
    TEST_ASSERT_NOT_NULL(json);
    cJSON *payload = cJSON_GetObjectItem(json, "payload");
    TEST_ASSERT_TRUE(cJSON_IsObject(payload));
    TEST_ASSERT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(payload, "a")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(payload, "b")));
    cJSON_Delete(json);

    free(result.tool_call_id);
    free(result.result);
    python_cleanup_tool_files();
    app_home_cleanup();
    rmdir_recursive(home);
}

static int registry_thread_safe(ToolRegistry *reg, const char *name) {
    for (size_t i = 0; i < reg->functions.count; i++) {
        if (strcmp(reg->functions.data[i].name, name) == 0) {
            return reg->functions.data[i].thread_safe;
        }
    }
    return -1;
}

void test_default_tools_thread_safety(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&registry));
    TEST_ASSERT_EQUAL_INT(0, register_python_tool(&registry));

    const char *safe[] = {"read_file", "search_files", "list_dir", "show_structure",
                          "search_history", "web_fetch", "shell", "write_file", "apply_patch"};
    for (size_t i = 0; i < sizeof(safe) / sizeof(safe[0]); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, registry_thread_safe(&registry, safe[i]), safe[i]);
    }
    TEST_ASSERT_EQUAL_INT(0, registry_thread_safe(&registry, "python"));
    TEST_ASSERT_EQUAL_INT(0, registry_thread_safe(&registry, "pip"));

    python_cleanup_tool_files();
}

void test_custom_tool_defaults_serial(void) {
    char home[] = "/tmp/custom_serial_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(home));
    app_home_cleanup();
    TEST_ASSERT_EQUAL_INT(0, app_home_init(home));

    char tools_dir[512], tool_path[600];
    snprintf(tools_dir, sizeof(tools_dir), "%s/tools", home);
    TEST_ASSERT_EQUAL_INT(0, mkdir(tools_dir, 0755));
    snprintf(tool_path, sizeof(tool_path), "%s/plain_tool.py", tools_dir);
    write_text_file(tool_path,
        "\"\"\"Plain tool.\n"
        "\n"
        "Gate: file_read\n"
        "\"\"\"\n"
        "\n"
        "def plain_tool(x: str) -> dict:\n"
        "    \"\"\"Echo x.\n"
        "\n"
        "    Args:\n"
        "        x: Input\n"
        "    \"\"\"\n"
        "    return {\"x\": x}\n");

    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&registry));
    TEST_ASSERT_EQUAL_INT(0, registry_thread_safe(&registry, "plain_tool"));

    python_cleanup_tool_files();
    app_home_cleanup();
    rmdir_recursive(home);
}

void test_custom_tool_without_module_docstring_is_serial(void) {
    char home[] = "/tmp/custom_nodoc_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(home));
    app_home_cleanup();
    TEST_ASSERT_EQUAL_INT(0, app_home_init(home));

    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    char tool_path[600];
    snprintf(tool_path, sizeof(tool_path), "%s/tools/nodoc_tool.py", home);
    write_text_file(tool_path,
        "def nodoc_tool(x: str) -> dict:\n"
        "    \"\"\"Echo x.\n"
        "\n"
        "    Args:\n"
        "        x: Input\n"
        "    \"\"\"\n"
        "    return {\"x\": x}\n");

    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_register_tool_schemas(&registry));
    TEST_ASSERT_EQUAL_INT(0, registry_thread_safe(&registry, "nodoc_tool"));
    TEST_ASSERT_NULL(python_tool_get_gate_category("nodoc_tool"));

    python_cleanup_tool_files();
    app_home_cleanup();
    rmdir_recursive(home);
}

static void *run_shell_sleep(void *arg) {
    ToolCall call = { .id = "par-shell", .name = "shell",
                      .arguments = "{\"command\": \"sleep 1\"}" };
    ToolResult *result = (ToolResult *)arg;
    execute_python_file_tool_call(&call, result);
    return NULL;
}

void test_parallel_shell_batch_overlaps(void) {
    TEST_ASSERT_EQUAL_INT(0, python_init_tool_files());
    TEST_ASSERT_EQUAL_INT(0, python_load_tool_files());

    enum { N = 3 };
    pthread_t threads[N];
    ToolResult results[N];
    memset(results, 0, sizeof(results));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(0, pthread_create(&threads[i], NULL, run_shell_sleep, &results[i]));
    }
    for (int i = 0; i < N; i++) pthread_join(threads[i], NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    for (int i = 0; i < N; i++) {
        TEST_ASSERT_EQUAL_INT(1, results[i].success);
        free(results[i].tool_call_id);
        free(results[i].result);
    }
    TEST_ASSERT_TRUE_MESSAGE(elapsed < 2.2, "three 1s shell calls did not overlap");

    python_cleanup_tool_files();
}

int main(void) {
    // Initialize app home before Python interpreter (needed for tool files path)
    app_home_init(NULL);

    // Initialize interpreter once for all tests
    if (python_interpreter_init() != 0) {
        fprintf(stderr, "Failed to initialize Python interpreter\n");
        app_home_cleanup();
        return 1;
    }

    UNITY_BEGIN();

    RUN_TEST(test_python_interpreter_init_succeeds);
    RUN_TEST(test_python_tool_through_registry);
    RUN_TEST(test_python_execute_basic_code);
    RUN_TEST(test_python_state_persists);
    RUN_TEST(test_python_function_persists);
    RUN_TEST(test_python_exception_handling);
    RUN_TEST(test_python_state_after_exception);
    RUN_TEST(test_python_timeout);
    RUN_TEST(test_python_stdlib_json);
    RUN_TEST(test_python_stdlib_math);
    RUN_TEST(test_shell_tool_exit_code_propagates);
    RUN_TEST(test_python_tool_docstring_parsing);
    RUN_TEST(test_sys_module_accessible);
    RUN_TEST(test_pip_version_key_ordering);
    RUN_TEST(test_pip_find_best_wheel);
    RUN_TEST(test_pip_safe_extractall_rejects_traversal);
    RUN_TEST(test_pip_list_empty_site_packages);
    RUN_TEST(test_read_file_returns_dict);
    RUN_TEST(test_search_files_new_features);
    RUN_TEST(test_list_dir_iso_timestamps);
    RUN_TEST(test_apply_patch_basic);
    RUN_TEST(test_retired_default_tools_removed_only_when_unmodified);
    RUN_TEST(test_file_tool_runs_on_non_main_thread);
    RUN_TEST(test_concurrent_calls_isolate_results);
    RUN_TEST(test_concurrent_calls_isolate_exceptions);
    RUN_TEST(test_invoke_nested_json_literals);
    RUN_TEST(test_interpreter_restart_after_gil_release);
    RUN_TEST(test_default_tools_thread_safety);
    RUN_TEST(test_custom_tool_defaults_serial);
    RUN_TEST(test_custom_tool_without_module_docstring_is_serial);
    RUN_TEST(test_parallel_shell_batch_overlaps);

    int result = UNITY_END();

    python_interpreter_shutdown();
    app_home_cleanup();

    return result;
}
