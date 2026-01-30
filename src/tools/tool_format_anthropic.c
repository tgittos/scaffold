/*
 * tool_format_anthropic.c - Anthropic tool format implementation
 *
 * Implements tool JSON generation and parsing for Anthropic Claude API.
 */

#include "tool_format.h"
#include "tools_system.h"
#include <stdio.h>

static char *anthropic_generate_tools_json(const ToolRegistry *registry) {
    if (registry == NULL || registry->function_count == 0) {
        return NULL;
    }

    cJSON *tools_array = cJSON_CreateArray();
    if (tools_array == NULL) {
        return NULL;
    }

    for (int i = 0; i < registry->function_count; i++) {
        const ToolFunction *func = &registry->functions[i];

        cJSON *tool = cJSON_CreateObject();
        if (tool == NULL) {
            cJSON_Delete(tools_array);
            return NULL;
        }

        if (!cJSON_AddStringToObject(tool, "name", func->name) ||
            !cJSON_AddStringToObject(tool, "description", func->description)) {
            cJSON_Delete(tool);
            cJSON_Delete(tools_array);
            return NULL;
        }

        cJSON *input_schema = cJSON_CreateObject();
        if (input_schema == NULL) {
            cJSON_Delete(tool);
            cJSON_Delete(tools_array);
            return NULL;
        }
        if (!cJSON_AddStringToObject(input_schema, "type", "object")) {
            cJSON_Delete(input_schema);
            cJSON_Delete(tool);
            cJSON_Delete(tools_array);
            return NULL;
        }

        if (func->parameter_count > 0) {
            cJSON *properties = cJSON_CreateObject();
            if (properties == NULL) {
                cJSON_Delete(input_schema);
                cJSON_Delete(tool);
                cJSON_Delete(tools_array);
                return NULL;
            }

            for (int j = 0; j < func->parameter_count; j++) {
                const ToolParameter *param = &func->parameters[j];

                cJSON *prop = cJSON_CreateObject();
                if (prop == NULL) {
                    cJSON_Delete(properties);
                    cJSON_Delete(input_schema);
                    cJSON_Delete(tool);
                    cJSON_Delete(tools_array);
                    return NULL;
                }

                if (!cJSON_AddStringToObject(prop, "type", param->type) ||
                    !cJSON_AddStringToObject(prop, "description", param->description)) {
                    cJSON_Delete(prop);
                    cJSON_Delete(properties);
                    cJSON_Delete(input_schema);
                    cJSON_Delete(tool);
                    cJSON_Delete(tools_array);
                    return NULL;
                }

                if (strcmp(param->type, "array") == 0) {
                    cJSON *items = cJSON_CreateObject();
                    if (items != NULL) {
                        cJSON_AddStringToObject(items, "type", "object");
                        cJSON_AddItemToObject(prop, "items", items);
                    }
                }

                if (param->enum_values != NULL && param->enum_count > 0) {
                    cJSON *enum_array = cJSON_CreateArray();
                    if (enum_array != NULL) {
                        for (int k = 0; k < param->enum_count; k++) {
                            cJSON_AddItemToArray(enum_array, cJSON_CreateString(param->enum_values[k]));
                        }
                        cJSON_AddItemToObject(prop, "enum", enum_array);
                    }
                }

                cJSON_AddItemToObject(properties, param->name, prop);
            }

            cJSON_AddItemToObject(input_schema, "properties", properties);

            cJSON *required = NULL;
            for (int j = 0; j < func->parameter_count; j++) {
                if (func->parameters[j].required) {
                    if (required == NULL) {
                        required = cJSON_CreateArray();
                        if (required == NULL) {
                            cJSON_Delete(input_schema);
                            cJSON_Delete(tool);
                            cJSON_Delete(tools_array);
                            return NULL;
                        }
                    }
                    cJSON_AddItemToArray(required, cJSON_CreateString(func->parameters[j].name));
                }
            }
            if (required != NULL) {
                cJSON_AddItemToObject(input_schema, "required", required);
            }
        }

        cJSON_AddItemToObject(tool, "input_schema", input_schema);
        cJSON_AddItemToArray(tools_array, tool);
    }

    char *json = cJSON_PrintUnformatted(tools_array);
    cJSON_Delete(tools_array);

    return json;
}

static int anthropic_parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    if (json_response == NULL || tool_calls == NULL || call_count == NULL) {
        return -1;
    }

    *tool_calls = NULL;
    *call_count = 0;

    const char *content_array = strstr(json_response, "\"content\":");
    if (content_array == NULL) {
        return 0;
    }

    const char *search_pos = content_array;
    int tool_count = 0;
    const char *pos = search_pos;
    while (pos != NULL) {
        const char *found = strstr(pos, "\"type\": \"tool_use\"");
        if (found == NULL) {
            found = strstr(pos, "\"type\":\"tool_use\"");
        }
        if (found == NULL) {
            break;
        }
        tool_count++;
        pos = found + strlen("\"type\": \"tool_use\"");
    }

    if (tool_count == 0) {
        return 0;
    }

    *tool_calls = malloc(tool_count * sizeof(ToolCall));
    if (*tool_calls == NULL) {
        return -1;
    }

    search_pos = content_array;
    int parsed_count = 0;

    while (parsed_count < tool_count) {
        const char *tool_use = strstr(search_pos, "\"type\": \"tool_use\"");
        if (tool_use == NULL) {
            tool_use = strstr(search_pos, "\"type\":\"tool_use\"");
        }
        if (tool_use == NULL) {
            break;
        }

        const char *obj_start = tool_use;
        while (obj_start > content_array && *obj_start != '{') {
            obj_start--;
        }

        if (*obj_start != '{') {
            search_pos = tool_use + 1;
            continue;
        }

        const char *obj_end = obj_start + 1;
        int brace_count = 1;
        while (*obj_end != '\0' && brace_count > 0) {
            if (*obj_end == '{' && (obj_end == json_response || *(obj_end-1) != '\\')) {
                brace_count++;
            } else if (*obj_end == '}' && (obj_end == json_response || *(obj_end-1) != '\\')) {
                brace_count--;
            }
            obj_end++;
        }

        if (brace_count != 0) {
            search_pos = tool_use + 1;
            continue;
        }

        size_t obj_len = obj_end - obj_start;
        char *tool_obj = malloc(obj_len + 1);
        if (tool_obj == NULL) {
            cleanup_tool_calls(*tool_calls, parsed_count);
            *tool_calls = NULL;
            return -1;
        }

        memcpy(tool_obj, obj_start, obj_len);
        tool_obj[obj_len] = '\0';

        ToolCall *call = &(*tool_calls)[parsed_count];
        call->id = tool_format_extract_string(tool_obj, "id");
        call->name = tool_format_extract_string(tool_obj, "name");
        call->arguments = tool_format_extract_object(tool_obj, "input");

        if (call->arguments == NULL) {
            call->arguments = tool_format_extract_string(tool_obj, "input");
            if (call->arguments == NULL) {
                call->arguments = strdup("{}");
            }
        }

        free(tool_obj);

        if (call->id == NULL || call->name == NULL) {
            if (call->id == NULL) {
                char id_buf[32];
                snprintf(id_buf, sizeof(id_buf), "anthropic_call_%d", parsed_count);
                call->id = strdup(id_buf);
            }

            if (call->name == NULL) {
                free(call->id);
                free(call->arguments);
                search_pos = obj_end;
                continue;
            }
        }

        parsed_count++;
        search_pos = obj_end;
    }

    *call_count = parsed_count;

    if (parsed_count == 0) {
        free(*tool_calls);
        *tool_calls = NULL;
    }

    return 0;
}

static char *anthropic_format_tool_result(const ToolResult *result) {
    if (result == NULL || result->tool_call_id == NULL || result->result == NULL) {
        return NULL;
    }

    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(obj, "type", "tool_result");
    cJSON_AddStringToObject(obj, "tool_use_id", result->tool_call_id);
    cJSON_AddStringToObject(obj, "content", result->result);

    char *json = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    return json;
}

const ToolFormatStrategy tool_format_anthropic = {
    .name = "anthropic",
    .generate_tools_json = anthropic_generate_tools_json,
    .parse_tool_calls = anthropic_parse_tool_calls,
    .format_tool_result = anthropic_format_tool_result
};

const ToolFormatStrategy *get_tool_format_strategy(const char *provider) {
    if (provider == NULL) {
        return &tool_format_openai;
    }

    if (strcmp(provider, "anthropic") == 0) {
        return &tool_format_anthropic;
    }

    return &tool_format_openai;
}
