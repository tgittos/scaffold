#include "tool_args.h"

#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Caller must call cJSON_Delete() on the returned object. */
static cJSON *parse_args(const ToolCall *tool_call) {
    if (tool_call == NULL || tool_call->arguments == NULL) {
        return NULL;
    }
    return cJSON_Parse(tool_call->arguments);
}

/*
 * Whether any object in the tree holds two members with the same name.
 * Names are compared case-insensitively, the way cJSON's lookup matches them.
 */
static int object_has_duplicate_keys(const cJSON *node) {
    for (const cJSON *child = node->child; child != NULL; child = child->next) {
        if (cJSON_IsObject(child) || cJSON_IsArray(child)) {
            if (object_has_duplicate_keys(child)) {
                return 1;
            }
        }
        if (child->string == NULL) {
            continue;
        }
        for (const cJSON *other = child->next; other != NULL; other = other->next) {
            if (other->string != NULL &&
                strcasecmp(child->string, other->string) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

int tool_args_has_duplicate_keys(const ToolCall *tool_call) {
    cJSON *args = parse_args(tool_call);
    if (args == NULL) {
        return 0;
    }
    int duplicated = object_has_duplicate_keys(args);
    cJSON_Delete(args);
    return duplicated;
}

char *tool_args_get_string(const ToolCall *tool_call, const char *key) {
    if (key == NULL) {
        return NULL;
    }

    cJSON *args = parse_args(tool_call);
    if (args == NULL) {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItemCaseSensitive(args, key);
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
        cJSON *item = cJSON_GetObjectItemCaseSensitive(args, path_keys[i]);
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

    cJSON *item = cJSON_GetObjectItemCaseSensitive(args, key);
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

    cJSON *item = cJSON_GetObjectItemCaseSensitive(args, key);
    int result = -1;
    if (cJSON_IsBool(item)) {
        *out_value = cJSON_IsTrue(item) ? 1 : 0;
        result = 0;
    }

    cJSON_Delete(args);
    return result;
}
