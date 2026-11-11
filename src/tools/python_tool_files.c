#define LOG_MODULE     LOG_MOD_TOOL
#define LOG_MODULE_STR "tool"
#include "../../lib/util/log.h"
#include "python_tool_files.h"
#include "python_tool.h"
#include "util/app_home.h"
#include <Python.h>
#include <cJSON.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

static PythonToolRegistry python_tool_registry = {0};
static int tool_files_initialized = 0;

static const char *DEFAULT_TOOL_NAMES[] = {
    "read_file",
    "write_file",
    "list_dir",
    "search_files",
    "apply_patch",
    "shell",
    "web_fetch",
    "pip",
    "search_history",
    "show_structure",
    NULL
};

static char* get_tools_dir_path(void) {
    return app_home_path(PYTHON_TOOLS_DIR_NAME);
}

static int mkdir_recursive(const char *path) {
    char *path_copy = strdup(path);
    if (path_copy == NULL) {
        return -1;
    }

    char *p = path_copy;
    if (*p == '/') p++;

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

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

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

/* Every version ever shipped of a default tool that has since been renamed or
 * removed. When retiring a default tool, add the SHA-256 of each of its shipped
 * versions here; files that match none are left alone as user edits. */
static const struct { const char *filename; const char *sha256_hex; } RETIRED_TOOLS[] = {
    {"append_file.py", "10c802edbe754c4f96f1d0ea840d29b441b9932512f2c5e48b1ffe3aa4df5023"},
    {"append_file.py", "246cbf4d1bd3a152bff2f3963b66676837983c009895a0a3c03554e062d9aed3"},
    {"append_file.py", "5c9d2e1d619c0cbc90404e64e04b28396192fc9441298abff809858fadac9369"},
    {"append_file.py", "66b7b83ab20ebe97b28299e733266550ed064b4372b4a512cb1ad2b61dcead32"},
    {"append_file.py", "83db1cbecdecfb0295d14e351d75011da1b3661fb305af98d1e33471c193e104"},
    {"append_file.py", "9dea43a276a12021abf24afbb5691ef0b0f0f24df8d87418c0893e9aa1ef58fc"},
    {"apply_delta.py", "0424e645fcd7164d09caaecee89e208f4b8b1ace23ee21dc61ff755e84261695"},
    {"apply_delta.py", "1e811089466a29368e594f487ac32747150794936bb3037f0b64750d1bf30265"},
    {"apply_delta.py", "686a128e7564b1b3411bb03310af3b8e05be5cf6d4b506a148a330cecfa8fd1e"},
    {"apply_delta.py", "a72a2412215dcb8b840a4d8b2f8cb262c16dbabd66be7f5b272ecd7aabc14dc8"},
    {"apply_delta.py", "a97ed9b7be6b42d790e3eb3f10f4bf8ab35e44d6aeee5b57b4f649dad766f42d"},
    {"apply_delta.py", "d83d51c932818f449a36fb0510c47612f44c4bca6c86013ee8312929a2a5b5e1"},
    {"file_info.py", "925897814a980fdda2e1fbaaeda879658f58e9da62a1c99fdd1c6e8acab0d9bd"},
    {"file_info.py", "aa7593f6ba99c750add2ea5737653508e2364399a1e3a87aa9cf4d2b854170e8"},
    {"file_info.py", "d385dce25d57da8f09508bdca27647186a4689e8a550b04751e26092762dd29a"},
    {"file_info.py", "f1e5e516460df8250b1a97f123cf6b7213e6dae63d09b138d301cae43dabdddd"},
    {"pip_install.py", "8fc71acd1d8306a0324d3ebd0707bf9c3560157d438d07957c8b0c4a5aa316e7"},
    {"pip_install.py", "e7f27b3f1fa7d4a17a64c798cdb7c0450ebd402c81732e6906f3d97efda6461e"},
    {"pip_install.py", "fe8f9100590724a4aeabb3e04c3db545a8b3575c618bd25a1251acc88dbe1424"},
    {"pip_list.py", "874d1fa2b570d41766f12b3df020899ba975c029176c97659e247c2e56bdea3f"},
};

static int sha256_hex_of_file(const char *path, char out[65]) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    int rc = mbedtls_sha256_starts(&ctx, 0);
    unsigned char buf[4096];
    size_t n;
    while (rc == 0 && (n = fread(buf, 1, sizeof(buf), f)) > 0) {
        rc = mbedtls_sha256_update(&ctx, buf, n);
    }
    if (ferror(f)) rc = -1;
    unsigned char digest[32];
    if (rc == 0) rc = mbedtls_sha256_finish(&ctx, digest);
    mbedtls_sha256_free(&ctx);
    fclose(f);
    if (rc != 0) return -1;

    for (int i = 0; i < 32; i++) {
        snprintf(out + i * 2, 3, "%02x", digest[i]);
    }
    return 0;
}

static void remove_retired_tools(const char *tools_dir) {
    for (size_t i = 0; i < sizeof(RETIRED_TOOLS) / sizeof(RETIRED_TOOLS[0]); i++) {
        char path[512];
        char hex[65];
        snprintf(path, sizeof(path), "%s/%s", tools_dir, RETIRED_TOOLS[i].filename);
        if (sha256_hex_of_file(path, hex) == 0 &&
            strcmp(hex, RETIRED_TOOLS[i].sha256_hex) == 0) {
            unlink(path);
        }
    }
}

static int extract_default_tools(const char *tools_dir) {
    int extracted = 0;

    for (int i = 0; DEFAULT_TOOL_NAMES[i] != NULL; i++) {
        char embedded_name[256];
        char dest_path[512];

        snprintf(embedded_name, sizeof(embedded_name), "%s.py", DEFAULT_TOOL_NAMES[i]);
        snprintf(dest_path, sizeof(dest_path), "%s/%s", tools_dir, embedded_name);

        // Skip if file already exists -- user may have customized it
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

    remove_retired_tools(tools_dir);

    return extracted;
}

int python_init_tool_files(void) {
    if (tool_files_initialized) {
        return 0;
    }

    char *tools_dir = get_tools_dir_path();
    if (tools_dir == NULL) {
        fprintf(stderr, "Error: Could not determine tools directory path\n");
        return -1;
    }

    if (!file_exists(tools_dir)) {
        if (mkdir_recursive(tools_dir) != 0) {
            fprintf(stderr, "Error: Could not create tools directory: %s\n", tools_dir);
            free(tools_dir);
            return -1;
        }
    }

    extract_default_tools(tools_dir);

    python_tool_registry.tools_dir = tools_dir;
    tool_files_initialized = 1;

    return 0;
}

static int extract_tool_schema(const char *func_name, PythonToolDef *tool_def) {
    if (!python_interpreter_is_initialized()) {
        return -1;
    }

    // Introspects function signature, docstring, and Gate:/Match: directives
    const char *schema_code =
        "def _ralph_parse_docstring_args(doc):\n"
        "    \"\"\"Parse Args section from Google-style docstring.\"\"\"\n"
        "    args_desc = {}\n"
        "    if not doc:\n"
        "        return args_desc\n"
        "    lines = doc.split('\\n')\n"
        "    in_args = False\n"
        "    current_param = None\n"
        "    current_desc = []\n"
        "    base_indent = 0\n"
        "    for line in lines:\n"
        "        stripped = line.strip()\n"
        "        if stripped == 'Args:' or stripped == 'Arguments:':\n"
        "            in_args = True\n"
        "            continue\n"
        "        if not in_args:\n"
        "            continue\n"
        "        # Check for end of Args section (Returns:, Raises:, etc.)\n"
        "        if stripped and stripped.endswith(':') and not stripped.startswith('-'):\n"
        "            if stripped in ('Returns:', 'Raises:', 'Yields:', 'Examples:', 'Note:', 'Notes:'):\n"
        "                break\n"
        "        # Calculate indentation\n"
        "        indent = len(line) - len(line.lstrip())\n"
        "        # New parameter line: has colon but doesn't start with dash\n"
        "        if ':' in stripped and not stripped.startswith('-') and not stripped.startswith('*'):\n"
        "            # Save previous parameter\n"
        "            if current_param:\n"
        "                args_desc[current_param] = ' '.join(current_desc)\n"
        "            parts = stripped.split(':', 1)\n"
        "            # Handle type annotations like 'param (type): desc'\n"
        "            param_part = parts[0].strip()\n"
        "            if '(' in param_part:\n"
        "                param_part = param_part.split('(')[0].strip()\n"
        "            current_param = param_part\n"
        "            current_desc = [parts[1].strip()] if len(parts) > 1 and parts[1].strip() else []\n"
        "            base_indent = indent\n"
        "        elif current_param and stripped:\n"
        "            # Continuation line for current parameter\n"
        "            current_desc.append(stripped)\n"
        "    # Save last parameter\n"
        "    if current_param:\n"
        "        args_desc[current_param] = ' '.join(current_desc)\n"
        "    return args_desc\n"
        "\n"
        "def _ralph_parse_gate_directives(doc):\n"
        "    \"\"\"Parse Gate: and Match: directives from docstring.\"\"\"\n"
        "    result = {'gate_category': None, 'match_arg': None}\n"
        "    if not doc:\n"
        "        return result\n"
        "    for line in doc.split('\\n'):\n"
        "        stripped = line.strip()\n"
        "        if stripped.startswith('Gate:'):\n"
        "            result['gate_category'] = stripped[5:].strip()\n"
        "        elif stripped.startswith('Match:'):\n"
        "            result['match_arg'] = stripped[6:].strip()\n"
        "    return result\n"
        "\n"
        "def _ralph_extract_schema(func_name):\n"
        "    import inspect\n"
        "    import json\n"
        "    import sys\n"
        "    func = globals().get(func_name)\n"
        "    if func is None or not callable(func):\n"
        "        return None\n"
        "    try:\n"
        "        sig = inspect.signature(func)\n"
        "        doc = func.__doc__ or ''\n"
        "        # Get module docstring for Gate:/Match: directives\n"
        "        main_module = sys.modules.get('__main__')\n"
        "        module_doc = getattr(main_module, '__doc__', '') or ''\n"
        "        gate_info = _ralph_parse_gate_directives(module_doc)\n"
        "        # Also check function docstring for directives\n"
        "        if gate_info['gate_category'] is None or gate_info['match_arg'] is None:\n"
        "            func_gate_info = _ralph_parse_gate_directives(doc)\n"
        "            if gate_info['gate_category'] is None:\n"
        "                gate_info['gate_category'] = func_gate_info['gate_category']\n"
        "            if gate_info['match_arg'] is None:\n"
        "                gate_info['match_arg'] = func_gate_info['match_arg']\n"
        "        # Get first line of docstring as description\n"
        "        desc = doc.split('\\n')[0].strip() if doc else func_name\n"
        "        # Parse Args section for parameter descriptions\n"
        "        arg_descriptions = _ralph_parse_docstring_args(doc)\n"
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
        "            # Use parsed docstring description or fall back to param name\n"
        "            p['description'] = arg_descriptions.get(name, name)\n"
        "            # Check for {PARAM}_ITEMS_SCHEMA constant for array item schemas\n"
        "            if p['type'] == 'array':\n"
        "                schema_var = name.upper() + '_ITEMS_SCHEMA'\n"
        "                schema_val = globals().get(schema_var)\n"
        "                if schema_val and isinstance(schema_val, dict):\n"
        "                    p['items_schema'] = json.dumps(schema_val)\n"
        "            params.append(p)\n"
        "        return json.dumps({\n"
        "            'name': func_name,\n"
        "            'description': desc,\n"
        "            'parameters': params,\n"
        "            'gate_category': gate_info['gate_category'],\n"
        "            'match_arg': gate_info['match_arg']\n"
        "        })\n"
        "    except Exception as e:\n"
        "        return None\n";

    PyObject *main_module = PyImport_AddModule("__main__");
    if (main_module == NULL) {
        return -1;
    }

    PyObject *globals = PyModule_GetDict(main_module);

    PyObject *result = PyRun_String(schema_code, Py_file_input, globals, globals);
    if (result == NULL) {
        PyErr_Clear();
        return -1;
    }
    Py_DECREF(result);

    char call_code[512];
    snprintf(call_code, sizeof(call_code),
             "_ralph_schema_result = _ralph_extract_schema('%s')", func_name);

    result = PyRun_String(call_code, Py_file_input, globals, globals);
    if (result == NULL) {
        PyErr_Clear();
        return -1;
    }
    Py_DECREF(result);

    PyObject *schema_result = PyDict_GetItemString(globals, "_ralph_schema_result");
    if (schema_result == NULL || schema_result == Py_None) {
        return -1;
    }

    const char *schema_json = PyUnicode_AsUTF8(schema_result);
    if (schema_json == NULL) {
        PyErr_Clear();
        return -1;
    }

    cJSON *schema = cJSON_Parse(schema_json);
    if (schema == NULL) {
        return -1;
    }

    cJSON *name_item = cJSON_GetObjectItem(schema, "name");
    cJSON *desc_item = cJSON_GetObjectItem(schema, "description");
    cJSON *params_item = cJSON_GetObjectItem(schema, "parameters");
    cJSON *gate_item = cJSON_GetObjectItem(schema, "gate_category");
    cJSON *match_item = cJSON_GetObjectItem(schema, "match_arg");

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

    // Extract gate category and match argument if specified.
    // These are optional metadata - NULL is acceptable (indicates not specified).
    // strdup failure for optional fields is handled gracefully by leaving NULL.
    tool_def->gate_category = NULL;
    tool_def->match_arg = NULL;
    if (cJSON_IsString(gate_item) && gate_item->valuestring != NULL) {
        tool_def->gate_category = strdup(gate_item->valuestring);
    }
    if (cJSON_IsString(match_item) && match_item->valuestring != NULL) {
        tool_def->match_arg = strdup(match_item->valuestring);
    }

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
                cJSON *p_items = cJSON_GetObjectItem(param, "items_schema");

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
                tool_def->parameters[i].items_schema = NULL;
                if (cJSON_IsString(p_items) && p_items->valuestring != NULL) {
                    tool_def->parameters[i].items_schema = strdup(p_items->valuestring);
                }
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

    // Prevent memory leak on double-init
    if (python_tool_registry.tools != NULL) {
        for (int i = 0; i < python_tool_registry.count; i++) {
            PythonToolDef *tool = &python_tool_registry.tools[i];
            free(tool->name);
            free(tool->description);
            free(tool->file_path);
            free(tool->gate_category);
            free(tool->match_arg);
            if (tool->parameters != NULL) {
                for (int j = 0; j < tool->parameter_count; j++) {
                    free(tool->parameters[j].name);
                    free(tool->parameters[j].type);
                    free(tool->parameters[j].description);
                    free(tool->parameters[j].items_schema);
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

    if (python_tool_registry.tools == NULL) {
        python_tool_registry.tools = calloc(MAX_PYTHON_TOOLS, sizeof(PythonToolDef));
        if (python_tool_registry.tools == NULL) {
            closedir(dir);
            return -1;
        }
    }

    PyGILState_STATE gil = PyGILState_Ensure();
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        size_t name_len = strlen(entry->d_name);
        if (name_len < 4 || strcmp(entry->d_name + name_len - 3, ".py") != 0) {
            continue;
        }

        if (python_tool_registry.count >= MAX_PYTHON_TOOLS) {
            fprintf(stderr, "Warning: Maximum number of Python tools reached\n");
            break;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 python_tool_registry.tools_dir, entry->d_name);

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

        char func_name[256];
        strncpy(func_name, entry->d_name, sizeof(func_name) - 1);
        func_name[name_len - 3] = '\0';

        PythonToolDef *tool_def = &python_tool_registry.tools[python_tool_registry.count];
        memset(tool_def, 0, sizeof(PythonToolDef));

        if (extract_tool_schema(func_name, tool_def) == 0) {
            tool_def->file_path = strdup(full_path);
            python_tool_registry.count++;
        }
    }
    PyGILState_Release(gil);

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

        // register_tool deep-copies, but we need our own copy since tool_def owns the originals
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
                params[j].items_schema = tool->parameters[j].items_schema ? strdup(tool->parameters[j].items_schema) : NULL;
            }
        }

        int result = register_tool(registry, tool->name, tool->description,
                                   params, tool->parameter_count,
                                   execute_python_file_tool_call);

        if (params != NULL) {
            for (int j = 0; j < tool->parameter_count; j++) {
                free(params[j].name);
                free(params[j].type);
                free(params[j].description);
                free(params[j].items_schema);
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

    // Freeform tool support: if the tool has a match_arg and arguments is not
    // JSON (doesn't start with '{'), treat the raw arguments string as the
    // value of the match_arg parameter, bypassing JSON parsing.  This is how
    // Codex models send apply_patch — raw patch text without JSON wrapping.
    const char *match_arg = python_tool_get_match_arg(tool_call->name);
    int freeform = (match_arg != NULL && tool_call->arguments != NULL
                    && tool_call->arguments[0] != '\0'
                    && tool_call->arguments[0] != '{');

    cJSON *args = NULL;
    if (freeform) {
        // Synthesize a JSON object with the match_arg as key
        args = cJSON_CreateObject();
        if (args != NULL) {
            cJSON_AddStringToObject(args, match_arg, tool_call->arguments);
        }
    } else {
        args = cJSON_Parse(tool_call->arguments);
    }

    if (args == NULL) {
        LOG_DEBUG("Failed to parse arguments for %s", tool_call->name);
        LOG_DEBUG("Arguments string: '%s'",
                  tool_call->arguments ? tool_call->arguments : "(NULL)");
        if (!freeform) {
            const char *error_ptr = cJSON_GetErrorPtr();
            LOG_DEBUG("cJSON error near: '%s'",
                      error_ptr ? error_ptr : "(unknown)");
        }

        result->result = strdup("{\"error\": \"Failed to parse arguments\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    // Two-pass approach: first calculate buffer size, then build kwargs string
    size_t kwargs_size_needed = 1;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, args) {
        if (cJSON_IsNull(item)) {
            continue;
        }
        kwargs_size_needed += 4;
        // Add space for "name="
        kwargs_size_needed += strlen(item->string) + 1;

        if (cJSON_IsString(item)) {
            // Worst case: every char escapes to 2 chars, plus quotes
            kwargs_size_needed += strlen(item->valuestring) * 2 + 3;
        } else if (cJSON_IsNumber(item)) {
            kwargs_size_needed += 32; // Enough for any number
        } else if (cJSON_IsBool(item)) {
            kwargs_size_needed += 6; // "False" is 5 chars
        } else if (cJSON_IsArray(item) || cJSON_IsObject(item)) {
            char *json_str = cJSON_PrintUnformatted(item);
            if (json_str) {
                kwargs_size_needed += strlen(json_str);
                free(json_str);
            }
        }
    }

    char *kwargs = malloc(kwargs_size_needed);
    if (kwargs == NULL) {
        cJSON_Delete(args);
        result->result = strdup("{\"error\": \"Memory allocation failed\", \"success\": false}");
        result->success = 0;
        return 0;
    }
    kwargs[0] = '\0';
    size_t kwargs_pos = 0;

    cJSON_ArrayForEach(item, args) {
        // Null values would cause Python syntax errors
        if (cJSON_IsNull(item)) {
            continue;
        }

        if (kwargs_pos > 0) {
            kwargs_pos += snprintf(kwargs + kwargs_pos, kwargs_size_needed - kwargs_pos, ", ");
        }

        if (cJSON_IsString(item)) {
            const char *str_val = item->valuestring;
            size_t str_len = strlen(str_val);
            size_t escaped_size = str_len * 2 + 1;
            char *escaped = malloc(escaped_size);
            if (escaped == NULL) {
                free(kwargs);
                cJSON_Delete(args);
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
            kwargs_pos += snprintf(kwargs + kwargs_pos, kwargs_size_needed - kwargs_pos,
                                   "%s='%s'", item->string, escaped);
            free(escaped);
        } else if (cJSON_IsNumber(item)) {
            if (item->valuedouble == (double)item->valueint) {
                kwargs_pos += snprintf(kwargs + kwargs_pos, kwargs_size_needed - kwargs_pos,
                                       "%s=%d", item->string, item->valueint);
            } else {
                kwargs_pos += snprintf(kwargs + kwargs_pos, kwargs_size_needed - kwargs_pos,
                                       "%s=%f", item->string, item->valuedouble);
            }
        } else if (cJSON_IsBool(item)) {
            kwargs_pos += snprintf(kwargs + kwargs_pos, kwargs_size_needed - kwargs_pos,
                                   "%s=%s", item->string, cJSON_IsTrue(item) ? "True" : "False");
        } else if (cJSON_IsArray(item) || cJSON_IsObject(item)) {
            char *json_str = cJSON_PrintUnformatted(item);
            if (json_str) {
                kwargs_pos += snprintf(kwargs + kwargs_pos, kwargs_size_needed - kwargs_pos,
                                       "%s=%s", item->string, json_str);
                free(json_str);
            }
        }
    }
    cJSON_Delete(args);

    size_t code_len = strlen(tool_call->name) + kwargs_pos + 1024;
    char *call_code = malloc(code_len);
    if (call_code == NULL) {
        free(kwargs);
        result->result = strdup("{\"error\": \"Memory allocation failed\", \"success\": false}");
        result->success = 0;
        return 0;
    }

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

    free(kwargs);

    PyGILState_STATE gil = PyGILState_Ensure();
    PyObject *main_module = PyImport_AddModule("__main__");
    if (main_module == NULL) {
        PyGILState_Release(gil);
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
        PyGILState_Release(gil);
        result->result = strdup("{\"error\": \"Python execution failed\", \"success\": false}");
        result->success = 0;
        return 0;
    }
    Py_DECREF(exec_result);

    PyObject *result_obj = PyDict_GetItemString(globals, "_ralph_result_json");
    if (result_obj == NULL) {
        PyGILState_Release(gil);
        result->result = strdup("{\"error\": \"No result from Python\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    const char *result_str = PyUnicode_AsUTF8(result_obj);
    if (result_str == NULL) {
        PyErr_Clear();
        PyGILState_Release(gil);
        result->result = strdup("{\"error\": \"Failed to get result string\", \"success\": false}");
        result->success = 0;
        return 0;
    }

    result->result = strdup(result_str);
    PyGILState_Release(gil);

    cJSON *result_json = cJSON_Parse(result->result);
    if (result_json != NULL) {
        cJSON *error_item = cJSON_GetObjectItem(result_json, "error");
        if (error_item != NULL && cJSON_IsString(error_item)) {
            // Explicit error field means failure
            result->success = 0;
        } else {
            // Check for exit_code (shell commands return this)
            // Non-zero exit_code means the command failed
            cJSON *exit_code = cJSON_GetObjectItemCaseSensitive(result_json, "exit_code");
            if (exit_code != NULL && cJSON_IsNumber(exit_code) && exit_code->valueint != 0) {
                result->success = 0;
            } else {
                result->success = 1;
            }
        }
        cJSON_Delete(result_json);
    } else {
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

const char* python_tool_get_gate_category(const char *name) {
    if (name == NULL) return NULL;

    for (int i = 0; i < python_tool_registry.count; i++) {
        if (python_tool_registry.tools[i].name != NULL &&
            strcmp(python_tool_registry.tools[i].name, name) == 0) {
            return python_tool_registry.tools[i].gate_category;
        }
    }
    return NULL;
}

const char* python_tool_get_match_arg(const char *name) {
    if (name == NULL) return NULL;

    for (int i = 0; i < python_tool_registry.count; i++) {
        if (python_tool_registry.tools[i].name != NULL &&
            strcmp(python_tool_registry.tools[i].name, name) == 0) {
            return python_tool_registry.tools[i].match_arg;
        }
    }
    return NULL;
}

void python_cleanup_tool_files(void) {
    if (python_tool_registry.tools != NULL) {
        for (int i = 0; i < python_tool_registry.count; i++) {
            PythonToolDef *tool = &python_tool_registry.tools[i];
            free(tool->name);
            free(tool->description);
            free(tool->file_path);
            free(tool->gate_category);
            free(tool->match_arg);
            if (tool->parameters != NULL) {
                for (int j = 0; j < tool->parameter_count; j++) {
                    free(tool->parameters[j].name);
                    free(tool->parameters[j].type);
                    free(tool->parameters[j].description);
                    free(tool->parameters[j].items_schema);
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
