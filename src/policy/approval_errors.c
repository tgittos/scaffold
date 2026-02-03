/*
 * approval_errors.c - Approval Gate Error Formatting
 *
 * JSON error message formatting for approval gate denials.
 * Extracted from approval_gate.c to improve modularity.
 */

#include "approval_gate.h"
#include "util/json_escape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *format_rate_limit_error(const ApprovalGateConfig *config,
                              const ToolCall *tool_call) {
    if (config == NULL || tool_call == NULL) {
        return NULL;
    }

    int remaining = get_rate_limit_remaining(config, tool_call->name);
    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    char *escaped_tool = json_escape_string(tool_name);
    if (escaped_tool == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"rate_limited\", "
                       "\"message\": \"Too many denied requests for %s tool. "
                       "Wait %d seconds before retrying.\", "
                       "\"retry_after\": %d, "
                       "\"tool\": \"%s\"}",
                       escaped_tool, remaining, remaining, escaped_tool);

    free(escaped_tool);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

char *format_denial_error(const ToolCall *tool_call) {
    if (tool_call == NULL) {
        return NULL;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    char *escaped_tool = json_escape_string(tool_name);
    if (escaped_tool == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"operation_denied\", "
                       "\"message\": \"User denied permission to execute %s\", "
                       "\"tool\": \"%s\", "
                       "\"suggestion\": \"Ask the user to perform this operation "
                       "manually, or request permission with explanation\"}",
                       escaped_tool, escaped_tool);

    free(escaped_tool);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

char *format_protected_file_error(const char *path) {
    if (path == NULL) {
        path = "unknown";
    }

    char *escaped_path = json_escape_string(path);
    if (escaped_path == NULL) {
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"protected_file\", "
                       "\"message\": \"Cannot modify protected configuration file\", "
                       "\"path\": \"%s\"}",
                       escaped_path);

    free(escaped_path);

    if (ret < 0) {
        return NULL;
    }
    return error;
}

char *format_non_interactive_error(const ToolCall *tool_call) {
    if (tool_call == NULL) {
        return NULL;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";
    GateCategory category = get_tool_category(tool_name);
    const char *category_name = gate_category_name(category);

    char *escaped_tool = json_escape_string(tool_name);
    char *escaped_category = json_escape_string(category_name);
    if (escaped_tool == NULL || escaped_category == NULL) {
        free(escaped_tool);
        free(escaped_category);
        return NULL;
    }

    char *error = NULL;
    int ret = asprintf(&error,
                       "{\"error\": \"non_interactive_gate\", "
                       "\"message\": \"Cannot execute %s operation without TTY for approval\", "
                       "\"tool\": \"%s\", "
                       "\"category\": \"%s\", "
                       "\"suggestion\": \"Use --yolo to bypass gates, or "
                       "--allow-category=%s to allow this category in non-interactive mode\"}",
                       escaped_category, escaped_tool, escaped_category, escaped_category);

    free(escaped_tool);
    free(escaped_category);

    if (ret < 0) {
        return NULL;
    }
    return error;
}
