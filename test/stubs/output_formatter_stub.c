#include "ui/output_formatter.h"

static bool g_json_output_mode = false;

void set_json_output_mode(bool enabled) {
    g_json_output_mode = enabled;
}

bool get_json_output_mode(void) {
    return g_json_output_mode;
}

char *extract_arg_summary(const char *tool_name, const char *arguments) {
    /* Stub: return NULL in tests */
    (void)tool_name;
    (void)arguments;
    return NULL;
}
