#include "tool_args.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

/* Caller must call cJSON_Delete() on the returned object. */
static cJSON *parse_args(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->arguments == NULL) {
        return NULL;
    }
    return cJSON_Parse(tool_call->arguments);
}

char *tool_args_get_string(const ToolCall *tool_call, const char *key) {
    if (key == NULL) {
        return NULL;
    }

    cJSON *args = parse_args(tool_call);
    if (args == NULL) {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItem(args, key);
    char *result = NULL;
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        result = strdup(item->valuestring);
    }

    cJSON_Delete(args);
    return result;
}

char *tool_args_get_command(const ToolCall *tool_call) {
    return tool_args_get_string(tool_call, "command");
}

char *tool_args_get_path(const ToolCall *tool_call) {
    /* Try common path argument names in order of preference */
    static const char *path_keys[] = {"path", "file_path", "filepath", "filename", NULL};

    cJSON *args = parse_args(tool_call);
    if (args == NULL) {
        return NULL;
    }

    char *result = NULL;
    for (int i = 0; path_keys[i] != NULL; i++) {
        cJSON *item = cJSON_GetObjectItem(args, path_keys[i]);
        if (cJSON_IsString(item) && item->valuestring != NULL) {
            result = strdup(item->valuestring);
            break;
        }
    }

    cJSON_Delete(args);
    return result;
}

char *tool_args_get_url(const ToolCall *tool_call) {
    return tool_args_get_string(tool_call, "url");
}

int tool_args_get_int(const ToolCall *tool_call, const char *key, int *out_value) {
    if (key == NULL || out_value == NULL) {
        return -1;
    }

    cJSON *args = parse_args(tool_call);
    if (args == NULL) {
        return -1;
    }

    cJSON *item = cJSON_GetObjectItem(args, key);
    int result = -1;
    if (cJSON_IsNumber(item)) {
        *out_value = item->valueint;
        result = 0;
    }

    cJSON_Delete(args);
    return result;
}

int tool_args_get_bool(const ToolCall *tool_call, const char *key, int *out_value) {
    if (key == NULL || out_value == NULL) {
        return -1;
    }

    cJSON *args = parse_args(tool_call);
    if (args == NULL) {
        return -1;
    }

    cJSON *item = cJSON_GetObjectItem(args, key);
    int result = -1;
    if (cJSON_IsBool(item)) {
        *out_value = cJSON_IsTrue(item) ? 1 : 0;
        result = 0;
    }

    cJSON_Delete(args);
    return result;
}
