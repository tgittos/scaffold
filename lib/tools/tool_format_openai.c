/*
 * tool_format_openai.c - OpenAI tool format implementation
 *
 * Implements tool JSON generation and parsing for OpenAI-compatible APIs.
 */

#include "tool_format.h"
#include "../../src/utils/json_escape.h"
#include <stdio.h>

static char *openai_generate_tools_json(const ToolRegistry *registry) {
    if (registry == NULL || registry->functions.count == 0) {
        return NULL;
    }

    size_t required_size = 2;

    for (size_t i = 0; i < registry->functions.count; i++) {
        const ToolFunction *func = &registry->functions.data[i];
        required_size += strlen(func->name) + strlen(func->description) + 200;

        if (i > 0) required_size += 2;

        for (int j = 0; j < func->parameter_count; j++) {
            const ToolParameter *param = &func->parameters[j];
            required_size += strlen(param->name) + strlen(param->type) + strlen(param->description) + 100;

            if (param->enum_values != NULL) {
                for (int k = 0; k < param->enum_count; k++) {
                    required_size += strlen(param->enum_values[k]) + 10;
                }
            }

            if (param->items_schema != NULL) {
                required_size += strlen(param->items_schema) + 20;
            }
        }
    }

    required_size *= 2;

    char *json = malloc(required_size);
    if (json == NULL) {
        return NULL;
    }

    size_t pos = 0;
    int ret = snprintf(json, required_size, "[");
    if (ret < 0 || (size_t)ret >= required_size) {
        free(json);
        return NULL;
    }
    pos += ret;

    for (size_t i = 0; i < registry->functions.count; i++) {
        const ToolFunction *func = &registry->functions.data[i];

        if (i > 0) {
            ret = snprintf(json + pos, required_size - pos, ", ");
            if (ret < 0 || (size_t)ret >= required_size - pos) {
                free(json);
                return NULL;
            }
            pos += ret;
        }

        char *escaped_desc = json_escape_string(func->description);
        if (escaped_desc == NULL) {
            free(json);
            return NULL;
        }
        ret = snprintf(json + pos, required_size - pos,
            "{\"type\": \"function\", \"function\": {\"name\": \"%s\", \"description\": \"%s\"",
            func->name, escaped_desc);
        free(escaped_desc);
        if (ret < 0 || (size_t)ret >= required_size - pos) {
            free(json);
            return NULL;
        }
        pos += ret;

        if (func->parameter_count > 0) {
            ret = snprintf(json + pos, required_size - pos, ", \"parameters\": {\"type\": \"object\", \"properties\": {");
            if (ret < 0 || (size_t)ret >= required_size - pos) {
                free(json);
                return NULL;
            }
            pos += ret;

            for (int j = 0; j < func->parameter_count; j++) {
                const ToolParameter *param = &func->parameters[j];

                if (j > 0) {
                    ret = snprintf(json + pos, required_size - pos, ", ");
                    if (ret < 0 || (size_t)ret >= required_size - pos) {
                        free(json);
                        return NULL;
                    }
                    pos += ret;
                }

                char *escaped_param_desc = json_escape_string(param->description);
                if (escaped_param_desc == NULL) {
                    free(json);
                    return NULL;
                }
                ret = snprintf(json + pos, required_size - pos,
                    "\"%s\": {\"type\": \"%s\", \"description\": \"%s\"",
                    param->name, param->type, escaped_param_desc);
                free(escaped_param_desc);
                if (ret < 0 || (size_t)ret >= required_size - pos) {
                    free(json);
                    return NULL;
                }
                pos += ret;

                if (strcmp(param->type, "array") == 0) {
                    if (param->items_schema != NULL) {
                        ret = snprintf(json + pos, required_size - pos, ", \"items\": %s", param->items_schema);
                    } else {
                        ret = snprintf(json + pos, required_size - pos, ", \"items\": {\"type\": \"object\"}");
                    }
                    if (ret < 0 || (size_t)ret >= required_size - pos) {
                        free(json);
                        return NULL;
                    }
                    pos += ret;
                }

                if (param->enum_values != NULL && param->enum_count > 0) {
                    ret = snprintf(json + pos, required_size - pos, ", \"enum\": [");
                    if (ret < 0 || (size_t)ret >= required_size - pos) {
                        free(json);
                        return NULL;
                    }
                    pos += ret;

                    for (int k = 0; k < param->enum_count; k++) {
                        if (k > 0) {
                            ret = snprintf(json + pos, required_size - pos, ", ");
                            if (ret < 0 || (size_t)ret >= required_size - pos) {
                                free(json);
                                return NULL;
                            }
                            pos += ret;
                        }
                        ret = snprintf(json + pos, required_size - pos, "\"%s\"", param->enum_values[k]);
                        if (ret < 0 || (size_t)ret >= required_size - pos) {
                            free(json);
                            return NULL;
                        }
                        pos += ret;
                    }

                    ret = snprintf(json + pos, required_size - pos, "]");
                    if (ret < 0 || (size_t)ret >= required_size - pos) {
                        free(json);
                        return NULL;
                    }
                    pos += ret;
                }

                ret = snprintf(json + pos, required_size - pos, "}");
                if (ret < 0 || (size_t)ret >= required_size - pos) {
                    free(json);
                    return NULL;
                }
                pos += ret;
            }

            ret = snprintf(json + pos, required_size - pos, "}, \"required\": [");
            if (ret < 0 || (size_t)ret >= required_size - pos) {
                free(json);
                return NULL;
            }
            pos += ret;

            int first_required = 1;
            for (int j = 0; j < func->parameter_count; j++) {
                if (func->parameters[j].required) {
                    if (!first_required) {
                        ret = snprintf(json + pos, required_size - pos, ", ");
                        if (ret < 0 || (size_t)ret >= required_size - pos) {
                            free(json);
                            return NULL;
                        }
                        pos += ret;
                    }
                    ret = snprintf(json + pos, required_size - pos, "\"%s\"", func->parameters[j].name);
                    if (ret < 0 || (size_t)ret >= required_size - pos) {
                        free(json);
                        return NULL;
                    }
                    pos += ret;
                    first_required = 0;
                }
            }

            ret = snprintf(json + pos, required_size - pos, "]}");
            if (ret < 0 || (size_t)ret >= required_size - pos) {
                free(json);
                return NULL;
            }
            pos += ret;
        }

        ret = snprintf(json + pos, required_size - pos, "}}");
        if (ret < 0 || (size_t)ret >= required_size - pos) {
            free(json);
            return NULL;
        }
        pos += ret;
    }

    ret = snprintf(json + pos, required_size - pos, "]");
    if (ret < 0 || (size_t)ret >= required_size - pos) {
        free(json);
        return NULL;
    }

    return json;
}

static int openai_parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    if (json_response == NULL || tool_calls == NULL || call_count == NULL) {
        return -1;
    }

    *tool_calls = NULL;
    *call_count = 0;

    const char *tool_calls_start = strstr(json_response, "\"tool_calls\":");
    if (tool_calls_start == NULL) {
        const char *custom_call_start = strstr(json_response, "<tool_call>");
        if (custom_call_start == NULL) {
            return 0;
        }

        const char *json_start = custom_call_start + strlen("<tool_call>");
        const char *json_end = strstr(json_start, "</tool_call>");
        if (json_end == NULL) {
            return -1;
        }

        while (*json_start == ' ' || *json_start == '\t' || *json_start == '\n' || *json_start == '\r') {
            json_start++;
        }

        size_t json_len = json_end - json_start;
        char *call_json = malloc(json_len + 1);
        if (call_json == NULL) {
            return -1;
        }

        memcpy(call_json, json_start, json_len);
        call_json[json_len] = '\0';

        ToolCall *call = malloc(sizeof(ToolCall));
        if (call == NULL) {
            free(call_json);
            return -1;
        }

        call->id = strdup("custom_call_1");
        call->name = tool_format_extract_string(call_json, "name");
        call->arguments = tool_format_extract_object(call_json, "arguments");

        if (call->arguments == NULL) {
            call->arguments = tool_format_extract_string(call_json, "arguments");
            if (call->arguments == NULL) {
                call->arguments = strdup("{}");
            }
        }

        free(call_json);

        if (call->name == NULL) {
            free(call->id);
            free(call->arguments);
            free(call);
            return -1;
        }

        *tool_calls = call;
        *call_count = 1;
        return 0;
    }

    const char *array_start = strchr(tool_calls_start, '[');
    if (array_start == NULL) {
        return -1;
    }

    int count = 0;
    const char *current = array_start + 1;
    while (*current != '\0' && *current != ']') {
        if (*current == '{') {
            count++;
            int brace_count = 1;
            current++;
            while (*current != '\0' && brace_count > 0) {
                if (*current == '{') {
                    brace_count++;
                } else if (*current == '}') {
                    brace_count--;
                }
                current++;
            }
        } else {
            current++;
        }
    }

    if (count == 0) {
        return 0;
    }

    ToolCall *calls = malloc(count * sizeof(ToolCall));
    if (calls == NULL) {
        return -1;
    }

    current = array_start + 1;
    int parsed_count = 0;

    while (*current != '\0' && *current != ']' && parsed_count < count) {
        while (*current == ' ' || *current == '\t' || *current == '\n' || *current == '\r' || *current == ',') {
            current++;
        }

        if (*current != '{') {
            current++;
            continue;
        }

        const char *call_start = current;
        const char *call_end = current + 1;
        int brace_count = 1;

        while (*call_end != '\0' && brace_count > 0) {
            if (*call_end == '{') {
                brace_count++;
            } else if (*call_end == '}') {
                brace_count--;
            }
            call_end++;
        }

        if (brace_count != 0) {
            for (int i = 0; i < parsed_count; i++) {
                free(calls[i].id);
                free(calls[i].name);
                free(calls[i].arguments);
            }
            free(calls);
            return -1;
        }

        size_t call_len = call_end - call_start;
        char *call_json = malloc(call_len + 1);
        if (call_json == NULL) {
            for (int i = 0; i < parsed_count; i++) {
                free(calls[i].id);
                free(calls[i].name);
                free(calls[i].arguments);
            }
            free(calls);
            return -1;
        }

        memcpy(call_json, call_start, call_len);
        call_json[call_len] = '\0';

        ToolCall *call = &calls[parsed_count];
        call->id = tool_format_extract_string(call_json, "id");
        call->name = NULL;
        call->arguments = NULL;

        char *function_obj = tool_format_extract_object(call_json, "function");
        if (function_obj != NULL) {
            call->name = tool_format_extract_string(function_obj, "name");
            call->arguments = tool_format_extract_string(function_obj, "arguments");
            free(function_obj);
        }

        free(call_json);

        if (call->id == NULL || call->name == NULL) {
            free(call->id);
            free(call->name);
            free(call->arguments);
            for (int i = 0; i < parsed_count; i++) {
                free(calls[i].id);
                free(calls[i].name);
                free(calls[i].arguments);
            }
            free(calls);
            return -1;
        }

        if (call->arguments == NULL) {
            call->arguments = strdup("{}");
        }

        parsed_count++;
        current = call_end;
    }

    *tool_calls = calls;
    *call_count = parsed_count;

    return 0;
}

static char *openai_format_tool_result(const ToolResult *result) {
    if (result == NULL || result->tool_call_id == NULL || result->result == NULL) {
        return NULL;
    }

    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(obj, "role", "tool");
    cJSON_AddStringToObject(obj, "tool_call_id", result->tool_call_id);
    cJSON_AddStringToObject(obj, "content", result->result);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    return json;
}

const ToolFormatStrategy tool_format_openai = {
    .name = "openai",
    .generate_tools_json = openai_generate_tools_json,
    .parse_tool_calls = openai_parse_tool_calls,
    .format_tool_result = openai_format_tool_result
};
