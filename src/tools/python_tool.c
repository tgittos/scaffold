#include "python_tool.h"
#include "../utils/json_escape.h"
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

// Static globals for persistent interpreter state
static PyObject *main_module = NULL;
static PyObject *globals_dict = NULL;
static int interpreter_initialized = 0;

// Timeout handling
static volatile sig_atomic_t python_timed_out = 0;
static struct sigaction old_sigaction;

static void python_timeout_handler(int sig) {
    (void)sig;
    python_timed_out = 1;
    PyErr_SetInterrupt();  // Raises KeyboardInterrupt in Python
}

int python_interpreter_init(void) {
    if (interpreter_initialized) {
        return 0;
    }

    // Set PYTHONHOME for embedded stdlib
    setenv("PYTHONHOME", "/zip", 1);

    // Disable bytecode writing since we're embedded
    setenv("PYTHONDONTWRITEBYTECODE", "1", 1);

    // Initialize Python
    Py_Initialize();

    if (!Py_IsInitialized()) {
        fprintf(stderr, "Failed to initialize Python interpreter\n");
        return -1;
    }

    // Get __main__ module and its globals dict for persistent state
    main_module = PyImport_AddModule("__main__");
    if (main_module == NULL) {
        fprintf(stderr, "Failed to get Python __main__ module\n");
        Py_Finalize();
        return -1;
    }

    globals_dict = PyModule_GetDict(main_module);
    if (globals_dict == NULL) {
        fprintf(stderr, "Failed to get Python globals dict\n");
        Py_Finalize();
        return -1;
    }

    // Keep reference to globals
    Py_INCREF(globals_dict);

    interpreter_initialized = 1;
    return 0;
}

void python_interpreter_shutdown(void) {
    if (!interpreter_initialized) {
        return;
    }

    if (globals_dict != NULL) {
        Py_DECREF(globals_dict);
        globals_dict = NULL;
    }

    main_module = NULL;  // Borrowed reference, don't decref

    Py_Finalize();
    interpreter_initialized = 0;
}

int python_interpreter_is_initialized(void) {
    return interpreter_initialized;
}

int register_python_tool(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    // Define parameters
    ToolParameter parameters[2];

    // Parameter 1: code (required)
    parameters[0].name = strdup("code");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("Python code to execute. Variables persist between calls.");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    // Parameter 2: timeout (optional)
    parameters[1].name = strdup("timeout");
    parameters[1].type = strdup("number");
    parameters[1].description = strdup("Maximum execution time in seconds (default: 30)");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

    // Check for allocation failures
    for (int i = 0; i < 2; i++) {
        if (parameters[i].name == NULL ||
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }

    // Register the tool
    int result = register_tool(registry, "python",
        "Execute Python code in a persistent interpreter. Variables, imports, and function definitions persist across calls. Use for calculations, data processing, and scripting tasks.",
        parameters, 2, execute_python_tool_call);

    // Clean up temporary parameter storage
    for (int i = 0; i < 2; i++) {
        free(parameters[i].name);
        free(parameters[i].type);
        free(parameters[i].description);
    }

    return result;
}

static char* extract_json_string_value(const char *json, const char *key) {
    char search_pattern[256] = {0};
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *key_pos = strstr(json, search_pattern);
    if (key_pos == NULL) {
        return NULL;
    }

    const char *colon_pos = strchr(key_pos, ':');
    if (colon_pos == NULL) {
        return NULL;
    }

    colon_pos++;
    while (*colon_pos == ' ' || *colon_pos == '\t' || *colon_pos == '\n') {
        colon_pos++;
    }

    if (*colon_pos != '"') {
        return NULL;
    }

    const char *start = colon_pos + 1;
    const char *end = start;

    // Parse JSON string with escape handling
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && *(end + 1) != '\0') {
            end += 2;
        } else {
            end++;
        }
    }

    if (*end != '"') {
        return NULL;
    }

    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }

    // Copy with escape sequence handling
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            char next = start[i + 1];
            switch (next) {
                case 'n': result[j++] = '\n'; i++; break;
                case 't': result[j++] = '\t'; i++; break;
                case 'r': result[j++] = '\r'; i++; break;
                case '\\': result[j++] = '\\'; i++; break;
                case '"': result[j++] = '"'; i++; break;
                case '/': result[j++] = '/'; i++; break;
                default: result[j++] = start[i]; break;
            }
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';

    return result;
}

static int extract_json_number_value(const char *json, const char *key, int default_value) {
    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *key_pos = strstr(json, search_pattern);
    if (key_pos == NULL) {
        return default_value;
    }

    const char *colon_pos = strchr(key_pos, ':');
    if (colon_pos == NULL) {
        return default_value;
    }

    colon_pos++;
    while (*colon_pos == ' ' || *colon_pos == '\t') {
        colon_pos++;
    }

    return atoi(colon_pos);
}

int parse_python_arguments(const char *json_args, PythonExecutionParams *params) {
    if (json_args == NULL || params == NULL) {
        return -1;
    }

    memset(params, 0, sizeof(PythonExecutionParams));

    params->code = extract_json_string_value(json_args, "code");
    if (params->code == NULL) {
        return -1;
    }

    // Validate code size
    if (strlen(params->code) > PYTHON_MAX_CODE_SIZE) {
        free(params->code);
        params->code = NULL;
        return -1;
    }

    params->timeout_seconds = extract_json_number_value(json_args, "timeout", PYTHON_DEFAULT_TIMEOUT);

    // Enforce reasonable timeout limits
    if (params->timeout_seconds <= 0) {
        params->timeout_seconds = PYTHON_DEFAULT_TIMEOUT;
    } else if (params->timeout_seconds > 300) {
        params->timeout_seconds = 300;  // Max 5 minutes
    }

    params->capture_stderr = 1;  // Always capture stderr separately

    return 0;
}

static char* capture_python_output(PyObject *string_io) {
    if (string_io == NULL) {
        return strdup("");
    }

    PyObject *getvalue = PyObject_GetAttrString(string_io, "getvalue");
    if (getvalue == NULL) {
        PyErr_Clear();
        return strdup("");
    }

    PyObject *output = PyObject_CallObject(getvalue, NULL);
    Py_DECREF(getvalue);

    if (output == NULL) {
        PyErr_Clear();
        return strdup("");
    }

    const char *output_str = PyUnicode_AsUTF8(output);
    char *result = output_str ? strdup(output_str) : strdup("");

    Py_DECREF(output);
    return result;
}

static char* get_python_exception_string(void) {
    PyObject *type, *value, *traceback;
    PyErr_Fetch(&type, &value, &traceback);

    if (type == NULL) {
        return NULL;
    }

    PyErr_NormalizeException(&type, &value, &traceback);

    char *result = NULL;

    // Try to get a detailed traceback
    PyObject *traceback_module = PyImport_ImportModule("traceback");
    if (traceback_module != NULL) {
        PyObject *format_exception = PyObject_GetAttrString(traceback_module, "format_exception");
        if (format_exception != NULL) {
            PyObject *args = PyTuple_Pack(3,
                type ? type : Py_None,
                value ? value : Py_None,
                traceback ? traceback : Py_None);
            if (args != NULL) {
                PyObject *formatted = PyObject_CallObject(format_exception, args);
                if (formatted != NULL) {
                    PyObject *joined = PyUnicode_Join(PyUnicode_FromString(""), formatted);
                    if (joined != NULL) {
                        const char *str = PyUnicode_AsUTF8(joined);
                        if (str != NULL) {
                            result = strdup(str);
                        }
                        Py_DECREF(joined);
                    }
                    Py_DECREF(formatted);
                }
                Py_DECREF(args);
            }
            Py_DECREF(format_exception);
        }
        Py_DECREF(traceback_module);
    }

    // Fallback to simple string representation
    if (result == NULL && value != NULL) {
        PyObject *str_value = PyObject_Str(value);
        if (str_value != NULL) {
            const char *str = PyUnicode_AsUTF8(str_value);
            if (str != NULL) {
                result = strdup(str);
            }
            Py_DECREF(str_value);
        }
    }

    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);

    PyErr_Clear();

    return result ? result : strdup("Unknown exception");
}

int execute_python_code(const PythonExecutionParams *params, PythonExecutionResult *result) {
    if (params == NULL || result == NULL || params->code == NULL) {
        return -1;
    }

    if (!interpreter_initialized) {
        if (python_interpreter_init() != 0) {
            return -1;
        }
    }

    memset(result, 0, sizeof(PythonExecutionResult));

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    // Import io module for StringIO
    PyObject *io_module = PyImport_ImportModule("io");
    if (io_module == NULL) {
        result->exception = strdup("Failed to import io module");
        result->success = 0;
        PyErr_Clear();
        return 0;
    }

    // Import sys module
    PyObject *sys_module = PyImport_ImportModule("sys");
    if (sys_module == NULL) {
        Py_DECREF(io_module);
        result->exception = strdup("Failed to import sys module");
        result->success = 0;
        PyErr_Clear();
        return 0;
    }

    // Create StringIO objects for capturing output
    PyObject *string_io_class = PyObject_GetAttrString(io_module, "StringIO");
    if (string_io_class == NULL) {
        Py_DECREF(io_module);
        Py_DECREF(sys_module);
        result->exception = strdup("Failed to get StringIO class");
        result->success = 0;
        PyErr_Clear();
        return 0;
    }

    PyObject *stdout_capture = PyObject_CallObject(string_io_class, NULL);
    PyObject *stderr_capture = PyObject_CallObject(string_io_class, NULL);

    if (stdout_capture == NULL || stderr_capture == NULL) {
        Py_XDECREF(stdout_capture);
        Py_XDECREF(stderr_capture);
        Py_DECREF(string_io_class);
        Py_DECREF(io_module);
        Py_DECREF(sys_module);
        result->exception = strdup("Failed to create StringIO objects");
        result->success = 0;
        PyErr_Clear();
        return 0;
    }

    // Save original stdout/stderr
    PyObject *original_stdout = PySys_GetObject("stdout");
    PyObject *original_stderr = PySys_GetObject("stderr");
    Py_XINCREF(original_stdout);
    Py_XINCREF(original_stderr);

    // Redirect stdout/stderr
    PySys_SetObject("stdout", stdout_capture);
    PySys_SetObject("stderr", stderr_capture);

    // Set up timeout handler
    python_timed_out = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = python_timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, &old_sigaction);

    if (params->timeout_seconds > 0) {
        alarm(params->timeout_seconds);
    }

    // Execute the Python code
    PyObject *exec_result = PyRun_String(params->code, Py_file_input, globals_dict, globals_dict);

    // Cancel alarm
    alarm(0);
    sigaction(SIGALRM, &old_sigaction, NULL);

    // Capture timing
    gettimeofday(&end_time, NULL);
    result->execution_time = (end_time.tv_sec - start_time.tv_sec) +
                            (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    // Check for timeout
    if (python_timed_out) {
        result->timed_out = 1;
        result->success = 0;
        result->exception = strdup("Execution timed out");
        PyErr_Clear();
    } else if (exec_result == NULL) {
        // Execution failed with exception
        result->success = 0;
        result->exception = get_python_exception_string();
    } else {
        result->success = 1;
        Py_DECREF(exec_result);
    }

    // Capture output
    result->stdout_output = capture_python_output(stdout_capture);
    result->stderr_output = capture_python_output(stderr_capture);

    // Truncate output if too large
    if (result->stdout_output && strlen(result->stdout_output) > PYTHON_MAX_OUTPUT_SIZE) {
        result->stdout_output[PYTHON_MAX_OUTPUT_SIZE] = '\0';
    }
    if (result->stderr_output && strlen(result->stderr_output) > PYTHON_MAX_OUTPUT_SIZE) {
        result->stderr_output[PYTHON_MAX_OUTPUT_SIZE] = '\0';
    }

    // Restore original stdout/stderr
    PySys_SetObject("stdout", original_stdout);
    PySys_SetObject("stderr", original_stderr);
    Py_XDECREF(original_stdout);
    Py_XDECREF(original_stderr);

    // Cleanup
    Py_DECREF(stdout_capture);
    Py_DECREF(stderr_capture);
    Py_DECREF(string_io_class);
    Py_DECREF(io_module);
    Py_DECREF(sys_module);

    return 0;
}

char* format_python_result_json(const PythonExecutionResult *exec_result) {
    if (exec_result == NULL) {
        return NULL;
    }

    // Escape strings for JSON
    char *escaped_stdout = json_escape_string(exec_result->stdout_output ? exec_result->stdout_output : "");
    char *escaped_stderr = json_escape_string(exec_result->stderr_output ? exec_result->stderr_output : "");
    char *escaped_exception = exec_result->exception ? json_escape_string(exec_result->exception) : NULL;

    if (escaped_stdout == NULL || escaped_stderr == NULL) {
        free(escaped_stdout);
        free(escaped_stderr);
        free(escaped_exception);
        return NULL;
    }

    size_t estimated_size = 512 + strlen(escaped_stdout) + strlen(escaped_stderr);
    if (escaped_exception) {
        estimated_size += strlen(escaped_exception);
    }

    char *json = malloc(estimated_size);
    if (json == NULL) {
        free(escaped_stdout);
        free(escaped_stderr);
        free(escaped_exception);
        return NULL;
    }

    snprintf(json, estimated_size,
        "{"
        "\"stdout\": \"%s\", "
        "\"stderr\": \"%s\", "
        "\"exception\": %s%s%s, "
        "\"success\": %s, "
        "\"execution_time\": %.3f, "
        "\"timed_out\": %s"
        "}",
        escaped_stdout,
        escaped_stderr,
        escaped_exception ? "\"" : "",
        escaped_exception ? escaped_exception : "null",
        escaped_exception ? "\"" : "",
        exec_result->success ? "true" : "false",
        exec_result->execution_time,
        exec_result->timed_out ? "true" : "false"
    );

    free(escaped_stdout);
    free(escaped_stderr);
    free(escaped_exception);

    return json;
}

int execute_python_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }

    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    // Check if interpreter is available
    if (!interpreter_initialized) {
        if (python_interpreter_init() != 0) {
            result->result = strdup("{\"error\": \"Python interpreter not available\", \"success\": false}");
            result->success = 0;
            return 0;
        }
    }

    PythonExecutionParams params;
    if (parse_python_arguments(tool_call->arguments, &params) != 0) {
        result->result = strdup("{\"error\": \"Failed to parse Python arguments\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    PythonExecutionResult exec_result;
    if (execute_python_code(&params, &exec_result) != 0) {
        cleanup_python_params(&params);
        result->result = strdup("{\"error\": \"Failed to execute Python code\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    result->result = format_python_result_json(&exec_result);
    result->success = exec_result.success;

    cleanup_python_params(&params);
    cleanup_python_result(&exec_result);

    if (result->result == NULL) {
        return -1;
    }

    return 0;
}

void cleanup_python_params(PythonExecutionParams *params) {
    if (params == NULL) {
        return;
    }

    free(params->code);
    memset(params, 0, sizeof(PythonExecutionParams));
}

void cleanup_python_result(PythonExecutionResult *result) {
    if (result == NULL) {
        return;
    }

    free(result->stdout_output);
    free(result->stderr_output);
    free(result->exception);
    memset(result, 0, sizeof(PythonExecutionResult));
}
