#include "python_tool_files.h"
#include "python_tool.h"
#include <Python.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

// Static storage for loaded Python tools
static PythonToolRegistry python_tool_registry = {0};
static int tool_files_initialized = 0;

// Names of the default Python tools (must match filenames without .py)
static const char *DEFAULT_TOOL_NAMES[] = {
    "read_file",
    "write_file",
    "append_file",
    "list_dir",
    "search_files",
    "file_info",
    "apply_delta",
    "shell",
    "web_fetch",
    NULL
};

// Get home directory
static char* get_home_dir(void) {
    const char *home = getenv("HOME");
    if (home != NULL) {
        return strdup(home);
    }

    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL) {
        return strdup(pw->pw_dir);
    }

    return NULL;
}

// Get or create the tools directory path
static char* get_tools_dir_path(void) {
    char *home = get_home_dir();
    if (home == NULL) {
        return NULL;
    }

    size_t len = strlen(home) + strlen(PYTHON_TOOLS_BASE_DIR) + strlen(PYTHON_TOOLS_DIR_NAME) + 4;
    char *path = malloc(len);
    if (path == NULL) {
        free(home);
        return NULL;
    }

    snprintf(path, len, "%s/%s/%s", home, PYTHON_TOOLS_BASE_DIR, PYTHON_TOOLS_DIR_NAME);
    free(home);
    return path;
}

// Create directory recursively
static int mkdir_recursive(const char *path) {
    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        return -1;
    }

    char *p = path_copy;
    if (*p == '/') p++;  // Skip leading slash

    while (*p != '\0') {
        while (*p != '/' && *p != '\0') p++;
        char saved = *p;
        *p = '\0';

        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            free(path_copy);
            return -1;
        }

        *p = saved;
        if (*p == '/') p++;
    }

    free(path_copy);
    return 0;
}

// Check if a file exists
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// Read embedded file from /zip/python_defaults/
static char* read_embedded_file(const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "/zip/python_defaults/%s", filename);

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 1024 * 1024) {  // 1MB limit
        fclose(f);
        return NULL;
    }

    char *content = malloc(size + 1);
    if (content == NULL) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(content, 1, size, f);
    fclose(f);

    content[read] = '\0';
    return content;
}

// Write content to a file
static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return (written == len) ? 0 : -1;
}

// Extract default Python tool files to ~/.local/ralph/tools/
static int extract_default_tools(const char *tools_dir) {
    int extracted = 0;

    for (int i = 0; DEFAULT_TOOL_NAMES[i] != NULL; i++) {
        char embedded_name[256];
        char dest_path[512];

        snprintf(embedded_name, sizeof(embedded_name), "%s.py", DEFAULT_TOOL_NAMES[i]);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", tools_dir, embedded_name);

        // Only extract if file doesn't exist (user may have modified it)
        if (file_exists(dest_path)) {
            continue;
        }

        char *content = read_embedded_file(embedded_name);
        if (content == NULL) {
            fprintf(stderr, "Warning: Could not read embedded file: %s\n", embedded_name);
            continue;
        }

        if (write_file(dest_path, content) == 0) {
            extracted++;
        } else {
            fprintf(stderr, "Warning: Could not write tool file: %s\n", dest_path);
        }

        free(content);
    }

    return extracted;
}

int python_init_tool_files(void) {
    if (tool_files_initialized) {
        return 0;
    }

    // Get tools directory path
    char *tools_dir = get_tools_dir_path();
    if (tools_dir == NULL) {
        fprintf(stderr, "Error: Could not determine tools directory path\n");
        return -1;
    }

    // Create directory if it doesn't exist
    if (!file_exists(tools_dir)) {
        if (mkdir_recursive(tools_dir) != 0) {
            fprintf(stderr, "Error: Could not create tools directory: %s\n", tools_dir);
            free(tools_dir);
            return -1;
        }
    }

    // Extract default tools if needed
    int extracted = extract_default_tools(tools_dir);
    if (extracted > 0) {
        // Successfully extracted some tools
    }

    // Store the tools directory path
    python_tool_registry.tools_dir = tools_dir;
    tool_files_initialized = 1;

    return 0;
}

// Parse Python function signature and docstring to create tool parameters
static int extract_tool_schema(const char *func_name, PythonToolDef *tool_def) {
    if (!python_interpreter_is_initialized()) {
        return -1;
    }

    // Python code to extract function schema
    const char *schema_code =
        "def _ralph_extract_schema(func_name):\n"
        "    import inspect\n"
        "    import json\n"
        "    func = globals().get(func_name)\n"
        "    if func is None or not callable(func):\n"
        "        return None\n"
        "    try:\n"
        "        sig = inspect.signature(func)\n"
        "        doc = func.__doc__ or ''\n"
        "        # Get first line of docstring as description\n"
        "        desc = doc.split('\\n')[0].strip() if doc else func_name\n"
        "        params = []\n"
        "        for name, param in sig.parameters.items():\n"
        "            p = {'name': name, 'type': 'string', 'required': True}\n"
        "            if param.annotation != inspect.Parameter.empty:\n"
        "                ann = param.annotation\n"
        "                if ann == str: p['type'] = 'string'\n"
        "                elif ann == int: p['type'] = 'number'\n"
        "                elif ann == float: p['type'] = 'number'\n"
        "                elif ann == bool: p['type'] = 'boolean'\n"
        "                elif ann == list: p['type'] = 'array'\n"
        "                elif ann == dict: p['type'] = 'object'\n"
        "            if param.default != inspect.Parameter.empty:\n"
        "                p['required'] = False\n"
        "            # Try to extract parameter description from docstring\n"
        "            p['description'] = name\n"
        "            params.append(p)\n"
        "        return json.dumps({'name': func_name, 'description': desc, 'parameters': params})\n"
        "    except Exception as e:\n"
        "        return None\n";

    // Execute the schema extraction code
    PyObject *main_module = PyImport_AddModule("__main__");
    if (main_module == NULL) {
        return -1;
    }

    PyObject *globals = PyModule_GetDict(main_module);

    // Define the extraction function
    PyObject *result = PyRun_String(schema_code, Py_file_input, globals, globals);
    if (result == NULL) {
        PyErr_Clear();
        return -1;
    }
    Py_DECREF(result);

    // Call the extraction function
    char call_code[512];
    snprintf(call_code, sizeof(call_code),
             "_ralph_schema_result = _ralph_extract_schema('%s')", func_name);

    result = PyRun_String(call_code, Py_file_input, globals, globals);
    if (result == NULL) {
        PyErr_Clear();
        return -1;
    }
    Py_DECREF(result);

    // Get the result
    PyObject *schema_result = PyDict_GetItemString(globals, "_ralph_schema_result");
    if (schema_result == NULL || schema_result == Py_None) {
        return -1;
    }

    const char *schema_json = PyUnicode_AsUTF8(schema_result);
    if (schema_json == NULL) {
        PyErr_Clear();
        return -1;
    }

    // Parse JSON schema
    cJSON *schema = cJSON_Parse(schema_json);
    if (schema == NULL) {
        return -1;
    }

    // Extract name and description
    cJSON *name_item = cJSON_GetObjectItem(schema, "name");
    cJSON *desc_item = cJSON_GetObjectItem(schema, "description");
    cJSON *params_item = cJSON_GetObjectItem(schema, "parameters");

    if (!cJSON_IsString(name_item) || !cJSON_IsString(desc_item)) {
        cJSON_Delete(schema);
        return -1;
    }

    tool_def->name = strdup(name_item->valuestring);
    if (tool_def->name == NULL) {
        cJSON_Delete(schema);
        return -1;
    }
    tool_def->description = strdup(desc_item->valuestring);
    if (tool_def->description == NULL) {
        free(tool_def->name);
        tool_def->name = NULL;
        cJSON_Delete(schema);
        return -1;
    }

    // Extract parameters
    if (cJSON_IsArray(params_item)) {
        int param_count = cJSON_GetArraySize(params_item);
        if (param_count > 0) {
            tool_def->parameters = calloc(param_count, sizeof(ToolParameter));
            if (tool_def->parameters == NULL) {
                free(tool_def->name);
                free(tool_def->description);
                cJSON_Delete(schema);
                return -1;
            }

            tool_def->parameter_count = param_count;

            for (int i = 0; i < param_count; i++) {
                cJSON *param = cJSON_GetArrayItem(params_item, i);
                cJSON *p_name = cJSON_GetObjectItem(param, "name");
                cJSON *p_type = cJSON_GetObjectItem(param, "type");
                cJSON *p_desc = cJSON_GetObjectItem(param, "description");
                cJSON *p_req = cJSON_GetObjectItem(param, "required");

                if (cJSON_IsString(p_name)) {
                    tool_def->parameters[i].name = strdup(p_name->valuestring);
                }
                if (cJSON_IsString(p_type)) {
                    tool_def->parameters[i].type = strdup(p_type->valuestring);
                }
                if (cJSON_IsString(p_desc)) {
                    tool_def->parameters[i].description = strdup(p_desc->valuestring);
                }
                tool_def->parameters[i].required = cJSON_IsTrue(p_req) ? 1 : 0;
                tool_def->parameters[i].enum_values = NULL;
                tool_def->parameters[i].enum_count = 0;
            }
        }
    }

    cJSON_Delete(schema);
    return 0;
}

int python_load_tool_files(void) {
    if (!tool_files_initialized || python_tool_registry.tools_dir == NULL) {
        fprintf(stderr, "Error: Tool files not initialized\n");
        return -1;
    }

    if (!python_interpreter_is_initialized()) {
        fprintf(stderr, "Error: Python interpreter not initialized\n");
        return -1;
    }

    // Free existing tools if reloading (prevents memory leak on double-init)
    if (python_tool_registry.tools != NULL) {
        for (int i = 0; i < python_tool_registry.count; i++) {
            PythonToolDef *tool = &python_tool_registry.tools[i];
            free(tool->name);
            free(tool->description);
            free(tool->file_path);
            if (tool->parameters != NULL) {
                for (int j = 0; j < tool->parameter_count; j++) {
                    free(tool->parameters[j].name);
                    free(tool->parameters[j].type);
                    free(tool->parameters[j].description);
                }
                free(tool->parameters);
            }
        }
        free(python_tool_registry.tools);
        python_tool_registry.tools = NULL;
        python_tool_registry.count = 0;
    }

    DIR *dir = opendir(python_tool_registry.tools_dir);
    if (dir == NULL) {
        fprintf(stderr, "Error: Could not open tools directory: %s\n",
                python_tool_registry.tools_dir);
        return -1;
    }

    // Allocate tools array
    if (python_tool_registry.tools == NULL) {
        python_tool_registry.tools = calloc(MAX_PYTHON_TOOLS, sizeof(PythonToolDef));
        if (python_tool_registry.tools == NULL) {
            closedir(dir);
            return -1;
        }
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip hidden files and non-.py files
        if (entry->d_name[0] == '.') continue;

        size_t name_len = strlen(entry->d_name);
        if (name_len < 4 || strcmp(entry->d_name + name_len - 3, ".py") != 0) {
            continue;
        }

        if (python_tool_registry.count >= MAX_PYTHON_TOOLS) {
            fprintf(stderr, "Warning: Maximum number of Python tools reached\n");
            break;
        }

        // Build full path
        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 python_tool_registry.tools_dir, entry->d_name);

        // Read the file content
        FILE *f = fopen(full_path, "r");
        if (f == NULL) {
            fprintf(stderr, "Warning: Could not read tool file: %s\n", full_path);
            continue;
        }

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size <= 0 || size > 1024 * 1024) {
            fclose(f);
            continue;
        }

        char *content = malloc(size + 1);
        if (content == NULL) {
            fclose(f);
            continue;
        }

        size_t read = fread(content, 1, size, f);
        fclose(f);
        content[read] = '\0';

        // Execute the Python file to load it into globals
        PyObject *main_module = PyImport_AddModule("__main__");
        if (main_module == NULL) {
            free(content);
            continue;
        }

        PyObject *globals = PyModule_GetDict(main_module);
        PyObject *result = PyRun_String(content, Py_file_input, globals, globals);
        free(content);

        if (result == NULL) {
            PyErr_Print();
            PyErr_Clear();
            continue;
        }
        Py_DECREF(result);

        // Extract the function name from the filename
        char func_name[256];
        strncpy(func_name, entry->d_name, sizeof(func_name) - 1);
        func_name[name_len - 3] = '\0';  // Remove .py extension

        // Extract tool schema
        PythonToolDef *tool_def = &python_tool_registry.tools[python_tool_registry.count];
        memset(tool_def, 0, sizeof(PythonToolDef));

        if (extract_tool_schema(func_name, tool_def) == 0) {
            tool_def->file_path = strdup(full_path);
            python_tool_registry.count++;
        }
    }

    closedir(dir);
    return 0;
}

int python_register_tool_schemas(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    if (python_tool_registry.count == 0) {
        return 0;  // No tools to register
    }

    for (int i = 0; i < python_tool_registry.count; i++) {
        PythonToolDef *tool = &python_tool_registry.tools[i];

        if (tool->name == NULL) continue;

        // Deep copy parameters for registration
        ToolParameter *params = NULL;
        if (tool->parameter_count > 0 && tool->parameters != NULL) {
            params = calloc(tool->parameter_count, sizeof(ToolParameter));
            if (params == NULL) continue;

            for (int j = 0; j < tool->parameter_count; j++) {
                params[j].name = tool->parameters[j].name ? strdup(tool->parameters[j].name) : NULL;
                params[j].type = tool->parameters[j].type ? strdup(tool->parameters[j].type) : NULL;
                params[j].description = tool->parameters[j].description ? strdup(tool->parameters[j].description) : NULL;
                params[j].required = tool->parameters[j].required;
                params[j].enum_values = NULL;
                params[j].enum_count = 0;
            }
        }

        int result = register_tool(registry, tool->name, tool->description,
                                   params, tool->parameter_count,
                                   execute_python_file_tool_call);

        // Free temporary parameter copies
        if (params != NULL) {
            for (int j = 0; j < tool->parameter_count; j++) {
                free(params[j].name);
                free(params[j].type);
                free(params[j].description);
            }
            free(params);
        }

        if (result != 0) {
            fprintf(stderr, "Warning: Failed to register Python tool: %s\n", tool->name);
        }
    }

    return 0;
}

int execute_python_file_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }

    result->tool_call_id = strdup(tool_call->id);
    if (result->tool_call_id == NULL) {
        return -1;
    }

    if (!python_interpreter_is_initialized()) {
        result->result = strdup("{\"error\": \"Python interpreter not initialized\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    // Parse the JSON arguments
    cJSON *args = cJSON_Parse(tool_call->arguments);
    if (args == NULL) {
        result->result = strdup("{\"error\": \"Failed to parse arguments\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    // Build Python function call
    // Convert JSON args to Python kwargs
    char *call_code = NULL;
    // Template code is ~700 bytes, kwargs can be up to 4096, plus function name
    // Allocate generous buffer to avoid truncation
    size_t code_len = strlen(tool_call->name) + 4096 + 1024;
    call_code = malloc(code_len);
    if (call_code == NULL) {
        cJSON_Delete(args);
        result->result = strdup("{\"error\": \"Memory allocation failed\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    // Build kwargs string from JSON
    // Skip null values to avoid syntax errors like "func(a='x', , b='y')"
    char kwargs[4096] = "";
    size_t kwargs_pos = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, args) {
        // Skip null values - they would cause syntax errors
        if (cJSON_IsNull(item)) {
            continue;
        }

        // Add comma separator after first argument
        if (kwargs_pos > 0) {
            kwargs_pos += snprintf(kwargs + kwargs_pos, sizeof(kwargs) - kwargs_pos, ", ");
        }

        if (cJSON_IsString(item)) {
            // Escape single quotes and backslashes in string values
            const char *str_val = item->valuestring;
            size_t str_len = strlen(str_val);
            // Worst case: every char escapes to 2 chars, plus null terminator
            size_t escaped_size = str_len * 2 + 1;
            char *escaped = malloc(escaped_size);
            if (escaped == NULL) {
                free(call_code);
                result->result = strdup("{\"error\": \"Memory allocation failed\", \"success\": false}");
                result->success = 0;
                return 0;
            }
            size_t e = 0;
            for (size_t j = 0; str_val[j]; j++) {
                if (str_val[j] == '\\') {
                    escaped[e++] = '\\';
                    escaped[e++] = '\\';
                } else if (str_val[j] == '\'') {
                    escaped[e++] = '\\';
                    escaped[e++] = '\'';
                } else if (str_val[j] == '\n') {
                    escaped[e++] = '\\';
                    escaped[e++] = 'n';
                } else if (str_val[j] == '\r') {
                    escaped[e++] = '\\';
                    escaped[e++] = 'r';
                } else if (str_val[j] == '\t') {
                    escaped[e++] = '\\';
                    escaped[e++] = 't';
                } else {
                    escaped[e++] = str_val[j];
                }
            }
            escaped[e] = '\0';
            kwargs_pos += snprintf(kwargs + kwargs_pos, sizeof(kwargs) - kwargs_pos,
                                   "%s='%s'", item->string, escaped);
            free(escaped);
        } else if (cJSON_IsNumber(item)) {
            if (item->valuedouble == (double)item->valueint) {
                kwargs_pos += snprintf(kwargs + kwargs_pos, sizeof(kwargs) - kwargs_pos,
                                       "%s=%d", item->string, item->valueint);
            } else {
                kwargs_pos += snprintf(kwargs + kwargs_pos, sizeof(kwargs) - kwargs_pos,
                                       "%s=%f", item->string, item->valuedouble);
            }
        } else if (cJSON_IsBool(item)) {
            kwargs_pos += snprintf(kwargs + kwargs_pos, sizeof(kwargs) - kwargs_pos,
                                   "%s=%s", item->string, cJSON_IsTrue(item) ? "True" : "False");
        } else if (cJSON_IsArray(item) || cJSON_IsObject(item)) {
            char *json_str = cJSON_PrintUnformatted(item);
            if (json_str) {
                kwargs_pos += snprintf(kwargs + kwargs_pos, sizeof(kwargs) - kwargs_pos,
                                       "%s=%s", item->string, json_str);
                free(json_str);
            }
        }
    }
    cJSON_Delete(args);

    // Build the Python execution code
    snprintf(call_code, code_len,
             "import json\n"
             "try:\n"
             "    _ralph_result = %s(%s)\n"
             "    if isinstance(_ralph_result, dict):\n"
             "        _ralph_result_json = json.dumps(_ralph_result)\n"
             "    elif isinstance(_ralph_result, str):\n"
             "        _ralph_result_json = json.dumps({'result': _ralph_result, 'success': True})\n"
             "    elif isinstance(_ralph_result, list):\n"
             "        _ralph_result_json = json.dumps({'results': _ralph_result, 'success': True})\n"
             "    else:\n"
             "        _ralph_result_json = json.dumps({'result': str(_ralph_result), 'success': True})\n"
             "except Exception as e:\n"
             "    import traceback\n"
             "    _ralph_result_json = json.dumps({'error': str(e), 'traceback': traceback.format_exc(), 'success': False})\n",
             tool_call->name, kwargs);

    // Execute the Python code
    PyObject *main_module = PyImport_AddModule("__main__");
    if (main_module == NULL) {
        free(call_code);
        result->result = strdup("{\"error\": \"Failed to get Python main module\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    PyObject *globals = PyModule_GetDict(main_module);
    PyObject *exec_result = PyRun_String(call_code, Py_file_input, globals, globals);
    free(call_code);

    if (exec_result == NULL) {
        PyErr_Print();
        PyErr_Clear();
        result->result = strdup("{\"error\": \"Python execution failed\", \"success\": false}");
        result->success = 0;
        return 0;
    }
    Py_DECREF(exec_result);

    // Get the result
    PyObject *result_obj = PyDict_GetItemString(globals, "_ralph_result_json");
    if (result_obj == NULL) {
        result->result = strdup("{\"error\": \"No result from Python\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    const char *result_str = PyUnicode_AsUTF8(result_obj);
    if (result_str == NULL) {
        PyErr_Clear();
        result->result = strdup("{\"error\": \"Failed to get result string\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    result->result = strdup(result_str);

    // Determine success: JSON parsed without an "error" field means success
    cJSON *result_json = cJSON_Parse(result_str);
    if (result_json != NULL) {
        cJSON *error_item = cJSON_GetObjectItem(result_json, "error");
        // Success if no error field present
        result->success = (error_item == NULL || !cJSON_IsString(error_item)) ? 1 : 0;
        cJSON_Delete(result_json);
    } else {
        // JSON didn't parse - treat as failure
        result->success = 0;
    }

    return 0;
}

int is_python_file_tool(const char *name) {
    if (name == NULL) return 0;

    for (int i = 0; i < python_tool_registry.count; i++) {
        if (python_tool_registry.tools[i].name != NULL &&
            strcmp(python_tool_registry.tools[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

const char* python_get_tools_dir(void) {
    return python_tool_registry.tools_dir;
}

char* python_get_loaded_tools_description(void) {
    if (python_tool_registry.count == 0) {
        return strdup("No Python tools loaded.");
    }

    // Calculate required size
    size_t size = 256;
    for (int i = 0; i < python_tool_registry.count; i++) {
        if (python_tool_registry.tools[i].name != NULL) {
            size += strlen(python_tool_registry.tools[i].name) + 4;
        }
    }

    char *desc = malloc(size);
    if (desc == NULL) return NULL;

    strcpy(desc, "Loaded Python tools: ");
    for (int i = 0; i < python_tool_registry.count; i++) {
        if (python_tool_registry.tools[i].name != NULL) {
            if (i > 0) strcat(desc, ", ");
            strcat(desc, python_tool_registry.tools[i].name);
        }
    }

    return desc;
}

void python_cleanup_tool_files(void) {
    if (python_tool_registry.tools != NULL) {
        for (int i = 0; i < python_tool_registry.count; i++) {
            PythonToolDef *tool = &python_tool_registry.tools[i];
            free(tool->name);
            free(tool->description);
            free(tool->file_path);
            if (tool->parameters != NULL) {
                for (int j = 0; j < tool->parameter_count; j++) {
                    free(tool->parameters[j].name);
                    free(tool->parameters[j].type);
                    free(tool->parameters[j].description);
                }
                free(tool->parameters);
            }
        }
        free(python_tool_registry.tools);
    }

    free(python_tool_registry.tools_dir);
    memset(&python_tool_registry, 0, sizeof(python_tool_registry));
    tool_files_initialized = 0;
}

int python_reset_tool_files(void) {
    if (!tool_files_initialized || python_tool_registry.tools_dir == NULL) {
        return -1;
    }

    // Backup existing files and re-extract defaults
    for (int i = 0; DEFAULT_TOOL_NAMES[i] != NULL; i++) {
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s.py",
                 python_tool_registry.tools_dir, DEFAULT_TOOL_NAMES[i]);

        if (file_exists(file_path)) {
            char backup_path[520];
            snprintf(backup_path, sizeof(backup_path), "%s.bak", file_path);
            rename(file_path, backup_path);
        }
    }

    return extract_default_tools(python_tool_registry.tools_dir);
}
