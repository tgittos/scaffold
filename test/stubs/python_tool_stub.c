/**
 * Python tool function stubs for testing.
 *
 * These stubs are used when Python tools aren't loaded (test environment).
 * In real usage, python_tool_files.c provides the actual implementations.
 */

int is_python_file_tool(const char *name) {
    (void)name;
    return 0; /* No Python tools loaded in test environment */
}

const char *python_tool_get_gate_category(const char *name) {
    (void)name;
    return NULL; /* No metadata available in test environment */
}

const char *python_tool_get_match_arg(const char *name) {
    (void)name;
    return NULL; /* No metadata available in test environment */
}
