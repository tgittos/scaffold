#include "python_tool.h"
#include "python_tool_files.h"
#include "../policy/verified_file_python.h"
#include <cJSON.h>
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

/* Track whether verified file module was registered (must be before Py_Initialize) */
static int verified_file_module_registered = 0;

/*
 * Static globals for persistent interpreter state.
 *
 * THREAD-SAFETY WARNING: This implementation is NOT thread-safe.
 * The interpreter state (main_module, globals_dict) and timeout handling
 * (python_timed_out, sigaction_saved) are stored in static global variables.
 * If multiple threads attempt to execute Python code concurrently:
 * - They will share the same globals dictionary, causing race conditions
 * - The timeout flag could be set/cleared incorrectly between threads
 * - Signal handlers may be restored incorrectly
 *
 * Python's GIL provides some protection for the Python interpreter itself,
 * but the C-level access to these objects is not synchronized.
 *
 * For thread-safe usage, callers must ensure that only one thread calls
 * execute_python_code() at a time, or add external synchronization.
 */
static PyObject *main_module = NULL;
static PyObject *globals_dict = NULL;
static int interpreter_initialized = 0;

/*
 * Timeout handling state.
 * The python_timed_out flag is shared across all invocations. In a multi-threaded
 * context, one thread's timeout could incorrectly affect another thread's execution.
 */
static volatile sig_atomic_t python_timed_out = 0;
static struct sigaction old_sigaction = {0};
static int sigaction_saved = 0;  /* Track whether old_sigaction was properly saved */

static void python_timeout_handler(int sig) {
    (void)sig;
    python_timed_out = 1;
    PyErr_SetInterrupt();  // Raises KeyboardInterrupt in Python
}

int python_interpreter_init(void) {
    if (interpreter_initialized) {
        return 0;
    }

    /* Py_Initialize() crashes if stdlib is missing; check first to fail gracefully */
    struct stat st;
    if (stat("/zip/lib/python3.12", &st) != 0 || !S_ISDIR(st.st_mode)) {
            return -1;
    }

    setenv("PYTHONHOME", "/zip", 1);
    setenv("PYTHONDONTWRITEBYTECODE", "1", 1);

    /* verified_file module must be registered before Py_Initialize() */
    if (!verified_file_module_registered) {
        if (verified_file_python_init() == 0) {
            verified_file_module_registered = 1;
        }
    }

    Py_Initialize();

    if (!Py_IsInitialized()) {
        fprintf(stderr, "Failed to initialize Python interpreter\n");
        return -1;
    }

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

    Py_INCREF(globals_dict);

    interpreter_initialized = 1;

    if (python_init_tool_files() != 0) {
        fprintf(stderr, "Warning: Failed to initialize Python tool files\n");
    }

    if (python_load_tool_files() != 0) {
        fprintf(stderr, "Warning: Failed to load Python tool files\n");
    }

    return 0;
}

void python_interpreter_shutdown(void) {
    if (!interpreter_initialized) {
        return;
    }

    /* Restore SIGALRM handler if it was modified for python timeouts.
     * This handles the case where shutdown is called while a signal handler
     * is still installed (e.g., after an error during Python execution). */
    if (sigaction_saved) {
        struct sigaction current_action;
        if (sigaction(SIGALRM, NULL, &current_action) == 0 &&
            current_action.sa_handler == python_timeout_handler) {
            /* Ignore errors restoring the signal handler during shutdown. */
            (void)sigaction(SIGALRM, &old_sigaction, NULL);
        }
        sigaction_saved = 0;
    }

    python_cleanup_tool_files();

    if (globals_dict != NULL) {
        Py_DECREF(globals_dict);
        globals_dict = NULL;
    }

    main_module = NULL;  /* Borrowed reference -- do not decref */

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

    /* Zero-initialize to ensure safe cleanup on allocation failure.
     * Without this, partial allocation failures could attempt to free
     * uninitialized (garbage) pointers in the cleanup loop. */
    ToolParameter parameters[2];
    memset(parameters, 0, sizeof(parameters));

    parameters[0].name = strdup("code");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("Python code to execute. Variables persist between calls.");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    parameters[1].name = strdup("timeout");
    parameters[1].type = strdup("number");
    parameters[1].description = strdup("Maximum execution time in seconds (default: 30)");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

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

    int result = register_tool(registry, "python",
        "Execute Python code in a persistent interpreter. Variables, imports, and function definitions persist across calls. Use for calculations, data processing, and scripting tasks.",
        parameters, 2, execute_python_tool_call);

    for (int i = 0; i < 2; i++) {
        free(parameters[i].name);
        free(parameters[i].type);
        free(parameters[i].description);
    }

    return result;
}

static char* extract_json_string_value(const char *json, const char *key) {
    cJSON *json_obj = cJSON_Parse(json);
    if (json_obj == NULL) {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(json_obj, key);
    char *result = NULL;
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        result = strdup(item->valuestring);
    }

    cJSON_Delete(json_obj);
    return result;
}

static int extract_json_number_value(const char *json, const char *key, int default_value) {
    cJSON *json_obj = cJSON_Parse(json);
    if (json_obj == NULL) {
        return default_value;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(json_obj, key);
    int result = default_value;
    if (cJSON_IsNumber(item)) {
        result = item->valueint;
    }

    cJSON_Delete(json_obj);
    return result;
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

    if (strlen(params->code) > PYTHON_MAX_CODE_SIZE) {
        free(params->code);
        params->code = NULL;
        return -1;
    }

    params->timeout_seconds = extract_json_number_value(json_args, "timeout", PYTHON_DEFAULT_TIMEOUT);

    if (params->timeout_seconds <= 0) {
        params->timeout_seconds = PYTHON_DEFAULT_TIMEOUT;
    } else if (params->timeout_seconds > PYTHON_MAX_TIMEOUT_SECONDS) {
        params->timeout_seconds = PYTHON_MAX_TIMEOUT_SECONDS;
    }

    params->capture_stderr = 1;  // Always capture stderr separately

    return 0;
}

static void truncate_output_if_needed(char *output, size_t max_size) {
    if (output == NULL) {
        return;
    }

    size_t len = strlen(output);
    if (len >= max_size) {
        const char *trunc_msg = "\n[Output truncated at 512KB]";
        size_t trunc_msg_len = strlen(trunc_msg);
        size_t start = max_size - trunc_msg_len - 1;
        memcpy(output + start, trunc_msg, trunc_msg_len);
        output[max_size - 1] = '\0';
    }
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
                    PyObject *empty_str = PyUnicode_FromString("");
                    if (empty_str != NULL) {
                        PyObject *joined = PyUnicode_Join(empty_str, formatted);
                        Py_DECREF(empty_str);
                        if (joined != NULL) {
                            const char *str = PyUnicode_AsUTF8(joined);
                            if (str != NULL) {
                                result = strdup(str);
                            }
                            Py_DECREF(joined);
                        }
                    }
                    Py_DECREF(formatted);
                }
                Py_DECREF(args);
            }
            Py_DECREF(format_exception);
        }
        Py_DECREF(traceback_module);
    }

    if (result == NULL) {
        char *type_name = NULL;
        char *value_str = NULL;

        if (type != NULL) {
            PyObject *type_name_obj = PyObject_GetAttrString(type, "__name__");
            if (type_name_obj != NULL) {
                const char *name = PyUnicode_AsUTF8(type_name_obj);
                if (name != NULL) {
                    type_name = strdup(name);
                }
                Py_DECREF(type_name_obj);
            }
        }

        if (value != NULL) {
            PyObject *str_value = PyObject_Str(value);
            if (str_value != NULL) {
                const char *str = PyUnicode_AsUTF8(str_value);
                if (str != NULL) {
                    value_str = strdup(str);
                }
                Py_DECREF(str_value);
            }
        }

        if (type_name && value_str) {
            size_t len = strlen(type_name) + strlen(value_str) + 3;  // ": " + null
            result = malloc(len);
            if (result) {
                snprintf(result, len, "%s: %s", type_name, value_str);
            }
        } else if (type_name) {
            result = type_name;
            type_name = NULL;  // Prevent double-free
        } else if (value_str) {
            result = value_str;
            value_str = NULL;  // Prevent double-free
        }

        free(type_name);
        free(value_str);
    }

    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);

    PyErr_Clear();

    return result ? result : strdup("Unknown exception");
}

int execute_python_code(const PythonExecutionParams *params, PythonExecutionResult *result) {
    if (params == NULL || result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(PythonExecutionResult));

    if (params->code == NULL) {
        result->success = 0;
        result->exception = strdup("No code provided");
        return 0;  // Return 0 with error in result, not -1
    }

    if (!interpreter_initialized) {
        if (python_interpreter_init() != 0) {
            result->success = 0;
            result->exception = strdup("Failed to initialize Python interpreter");
            return 0;  // Return 0 with error in result, not -1
        }
    }

    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    PyObject *io_module = PyImport_ImportModule("io");
    if (io_module == NULL) {
        result->exception = strdup("Failed to import io module");
        result->success = 0;
        PyErr_Clear();
        return 0;
    }

    PyObject *sys_module = PyImport_ImportModule("sys");
    if (sys_module == NULL) {
        Py_DECREF(io_module);
        result->exception = strdup("Failed to import sys module");
        result->success = 0;
        PyErr_Clear();
        return 0;
    }

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

    PyObject *original_stdout = PySys_GetObject("stdout");
    PyObject *original_stderr = PySys_GetObject("stderr");
    Py_XINCREF(original_stdout);
    Py_XINCREF(original_stderr);

    PySys_SetObject("stdout", stdout_capture);
    PySys_SetObject("stderr", stderr_capture);

    python_timed_out = 0;
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = python_timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, &old_sigaction) == 0) {
        sigaction_saved = 1;
    } else {
        fprintf(stderr, "Warning: Failed to set up Python timeout handler\n");
    }

    if (params->timeout_seconds > 0) {
        alarm(params->timeout_seconds);
    }

    PyObject *exec_result = PyRun_String(params->code, Py_file_input, globals_dict, globals_dict);

    /* Block SIGALRM during cleanup to prevent race between alarm(0) and sigaction restore */
    {
        sigset_t sigalrm_set, old_set;
        sigemptyset(&sigalrm_set);
        sigaddset(&sigalrm_set, SIGALRM);
        sigprocmask(SIG_BLOCK, &sigalrm_set, &old_set);

        alarm(0);
        if (sigaction_saved) {
            sigaction(SIGALRM, &old_sigaction, NULL);
            sigaction_saved = 0;
        }

        sigprocmask(SIG_SETMASK, &old_set, NULL);
    }

    gettimeofday(&end_time, NULL);
    result->execution_time = (end_time.tv_sec - start_time.tv_sec) +
                            (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    if (python_timed_out) {
        result->timed_out = 1;
        result->success = 0;
        result->exception = strdup("Execution timed out");
        PyErr_Clear();
    } else if (exec_result == NULL) {
        result->success = 0;
        result->exception = get_python_exception_string();
    } else {
        result->success = 1;
        Py_DECREF(exec_result);
    }

    result->stdout_output = capture_python_output(stdout_capture);
    result->stderr_output = capture_python_output(stderr_capture);

    truncate_output_if_needed(result->stdout_output, PYTHON_MAX_OUTPUT_SIZE);
    truncate_output_if_needed(result->stderr_output, PYTHON_MAX_OUTPUT_SIZE);

    PySys_SetObject("stdout", original_stdout);
    PySys_SetObject("stderr", original_stderr);
    Py_XDECREF(original_stdout);
    Py_XDECREF(original_stderr);

    Py_DECREF(stdout_capture);
    Py_DECREF(stderr_capture);
    Py_DECREF(string_io_class);
    Py_DECREF(io_module);
    Py_DECREF(sys_module);

    return 0;
}

/*
 * Format Python execution result as JSON string.
 *
 * Uses cJSON for proper JSON escaping. Note that cJSON handles Unicode escape
 * sequences (\uXXXX) for the Basic Multilingual Plane (codepoints up to 0xFFFF),
 * but does not handle surrogate pairs for codepoints above 0xFFFF. This is
 * acceptable for typical Python tool output which is primarily ASCII/UTF-8.
 */
char* format_python_result_json(const PythonExecutionResult *exec_result) {
    if (exec_result == NULL) {
        return NULL;
    }

    /* Build JSON using cJSON for proper escaping and formatting */
    cJSON *json_obj = cJSON_CreateObject();
    if (json_obj == NULL) {
        return NULL;
    }

    if (!cJSON_AddStringToObject(json_obj, "stdout",
            exec_result->stdout_output ? exec_result->stdout_output : "")) {
        cJSON_Delete(json_obj);
        return NULL;
    }

    if (!cJSON_AddStringToObject(json_obj, "stderr",
            exec_result->stderr_output ? exec_result->stderr_output : "")) {
        cJSON_Delete(json_obj);
        return NULL;
    }

    if (exec_result->exception) {
        if (!cJSON_AddStringToObject(json_obj, "exception", exec_result->exception)) {
            cJSON_Delete(json_obj);
            return NULL;
        }
    } else {
        if (!cJSON_AddNullToObject(json_obj, "exception")) {
            cJSON_Delete(json_obj);
            return NULL;
        }
    }

    if (!cJSON_AddBoolToObject(json_obj, "success", exec_result->success)) {
        cJSON_Delete(json_obj);
        return NULL;
    }

    if (!cJSON_AddNumberToObject(json_obj, "execution_time", exec_result->execution_time)) {
        cJSON_Delete(json_obj);
        return NULL;
    }

    if (!cJSON_AddBoolToObject(json_obj, "timed_out", exec_result->timed_out)) {
        cJSON_Delete(json_obj);
        return NULL;
    }

    char *json_str = cJSON_PrintUnformatted(json_obj);
    cJSON_Delete(json_obj);

    return json_str;
}

int execute_python_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }

    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    if (!interpreter_initialized) {
        if (python_interpreter_init() != 0) {
            result->result = strdup("{\"error\": \"Python interpreter not available\", \"success\": false}");
            if (result->result == NULL) {
                return -1;
            }
            result->success = 0;
            return 0;
        }
    }

    PythonExecutionParams params;
    if (parse_python_arguments(tool_call->arguments, &params) != 0) {
        result->result = strdup("{\"error\": \"Failed to parse Python arguments\", \"success\": false}");
        if (result->result == NULL) {
            return -1;
        }
        result->success = 0;
        return 0;
    }

    PythonExecutionResult exec_result;
    if (execute_python_code(&params, &exec_result) != 0) {
        cleanup_python_params(&params);
        result->result = strdup("{\"error\": \"Failed to execute Python code\", \"success\": false}");
        if (result->result == NULL) {
            return -1;
        }
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
