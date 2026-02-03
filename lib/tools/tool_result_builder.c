#include "tool_result_builder.h"
#include "../util/common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

struct tool_result_builder {
    char* tool_call_id;
    char* result_content;
    int success;
    int finalized;
};

tool_result_builder_t* tool_result_builder_create(const char* tool_call_id) {
    if (tool_call_id == NULL) {
        return NULL;
    }

    tool_result_builder_t* builder = malloc(sizeof(tool_result_builder_t));
    if (builder == NULL) {
        return NULL;
    }

    builder->tool_call_id = safe_strdup(tool_call_id);
    builder->result_content = NULL;
    builder->success = 0;
    builder->finalized = 0;

    if (builder->tool_call_id == NULL) {
        free(builder);
        return NULL;
    }

    return builder;
}

int tool_result_builder_set_success(tool_result_builder_t* builder, const char* format, ...) {
    if (builder == NULL || format == NULL || builder->finalized) {
        return -1;
    }

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return -1;
    }

    free(builder->result_content);
    builder->result_content = malloc(needed + 1);
    if (builder->result_content == NULL) {
        va_end(args);
        return -1;
    }

    vsnprintf(builder->result_content, needed + 1, format, args);
    va_end(args);

    builder->success = 1;
    return 0;
}

int tool_result_builder_set_error(tool_result_builder_t* builder, const char* format, ...) {
    if (builder == NULL || format == NULL || builder->finalized) {
        return -1;
    }

    va_list args;
    va_start(args, format);

    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return -1;
    }

    char* error_msg = malloc(needed + 1);
    if (error_msg == NULL) {
        va_end(args);
        return -1;
    }

    vsnprintf(error_msg, needed + 1, format, args);
    va_end(args);

    free(builder->result_content);
    builder->result_content = create_error_message("{\"success\": false, \"error\": \"%s\"}", error_msg);
    free(error_msg);

    if (builder->result_content == NULL) {
        return -1;
    }

    builder->success = 0;
    return 0;
}

int tool_result_builder_set_success_json(tool_result_builder_t* builder, const char* json_object) {
    if (builder == NULL || json_object == NULL || builder->finalized) {
        return -1;
    }

    free(builder->result_content);
    builder->result_content = safe_strdup(json_object);

    if (builder->result_content == NULL) {
        return -1;
    }

    builder->success = 1;
    return 0;
}

int tool_result_builder_set_error_json(tool_result_builder_t* builder, const char* error_message) {
    if (builder == NULL || error_message == NULL || builder->finalized) {
        return -1;
    }

    free(builder->result_content);
    builder->result_content = create_error_message("{\"success\": false, \"error\": \"%s\"}", error_message);

    if (builder->result_content == NULL) {
        return -1;
    }

    builder->success = 0;
    return 0;
}

ToolResult* tool_result_builder_finalize(tool_result_builder_t* builder) {
    if (builder == NULL || builder->finalized) {
        return NULL;
    }

    ToolResult* result = malloc(sizeof(ToolResult));
    if (result == NULL) {
        tool_result_builder_destroy(builder);
        return NULL;
    }

    result->tool_call_id = builder->tool_call_id;
    result->result = builder->result_content;
    result->success = builder->success;

    builder->tool_call_id = NULL;
    builder->result_content = NULL;
    builder->finalized = 1;

    tool_result_builder_destroy(builder);
    return result;
}

void tool_result_builder_destroy(tool_result_builder_t* builder) {
    if (builder != NULL) {
        free(builder->tool_call_id);
        free(builder->result_content);
        free(builder);
    }
}
