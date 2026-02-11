#include "mode_tool.h"
#include "tool_param_dsl.h"
#include "tool_result_builder.h"
#include "../agent/prompt_mode.h"
#include "../agent/session.h"
#include "../ui/status_line.h"
#include "../util/common_utils.h"
#include <stdio.h>
#include <string.h>

static AgentSession* g_session = NULL;

void mode_tool_set_session(AgentSession* session) {
    g_session = session;
}

int execute_switch_mode_tool_call(const ToolCall* tool_call, ToolResult* result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    if (g_session == NULL) {
        tool_result_builder_set_error(builder, "Mode system not initialized");
        ToolResult* tmp = tool_result_builder_finalize(builder);
        if (tmp) { *result = *tmp; free(tmp); }
        return 0;
    }

    char* mode_name = extract_string_param(tool_call->arguments, "mode");
    if (mode_name == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: mode");
        ToolResult* tmp = tool_result_builder_finalize(builder);
        if (tmp) { *result = *tmp; free(tmp); }
        return 0;
    }

    PromptMode new_mode;
    if (prompt_mode_from_name(mode_name, &new_mode) != 0) {
        tool_result_builder_set_error(builder,
            "Unknown mode '%s'. Valid modes: default, plan, explore, debug, review", mode_name);
        free(mode_name);
        ToolResult* tmp = tool_result_builder_finalize(builder);
        if (tmp) { *result = *tmp; free(tmp); }
        return 0;
    }

    PromptMode old_mode = g_session->current_mode;
    g_session->current_mode = new_mode;
    status_line_set_mode(new_mode);

    tool_result_builder_set_success(builder,
        "Switched from %s to %s mode. %s",
        prompt_mode_name(old_mode), prompt_mode_name(new_mode),
        prompt_mode_description(new_mode));

    free(mode_name);
    ToolResult* tmp = tool_result_builder_finalize(builder);
    if (tmp) { *result = *tmp; free(tmp); }
    return 0;
}

static const char* MODE_ENUM_VALUES[] = {
    "default", "plan", "explore", "debug", "review", NULL
};

static const ParamDef SWITCH_MODE_PARAMS[] = {
    {
        .name = "mode",
        .type = "string",
        .description = "The behavioral mode to switch to",
        .enum_values = MODE_ENUM_VALUES,
        .required = 1,
    },
};

static const ToolDef SWITCH_MODE_DEF = {
    .name = "switch_mode",
    .description = "Switch the agent's behavioral mode to adjust approach for the current task. "
                   "Use 'plan' for structured planning, 'explore' for code reading, "
                   "'debug' for systematic diagnosis, 'review' for code quality assessment, "
                   "or 'default' to reset.",
    .params = SWITCH_MODE_PARAMS,
    .param_count = 1,
    .execute = execute_switch_mode_tool_call,
};

int register_mode_tool(ToolRegistry* registry) {
    if (registry == NULL) return -1;
    return register_tool_from_def(registry, &SWITCH_MODE_DEF);
}
