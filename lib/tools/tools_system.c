#include "tools_system.h"
#include "tool_format.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DARRAY_DEFINE(ToolFunctionArray, ToolFunction)

static void free_tool_function_contents(ToolFunction *func) {
    if (!func) return;
    free(func->name);
    free(func->description);
    for (int j = 0; j < func->parameter_count; j++) {
        ToolParameter *param = &func->parameters[j];
        free(param->name);
        free(param->type);
        free(param->description);
        free(param->items_schema);
        for (int k = 0; k < param->enum_count; k++) {
            free(param->enum_values[k]);
        }
        free(param->enum_values);
    }
    free(func->parameters);
}

void init_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    ToolFunctionArray_init(&registry->functions);
    registry->services = NULL;
}

int register_tool(ToolRegistry *registry, const char *name, const char *description,
                  ToolParameter *parameters, int param_count, tool_execute_func_t execute_func) {
    if (registry == NULL || name == NULL || description == NULL || execute_func == NULL) {
        return -1;
    }

    ToolFunction func = {0};
    func.name = strdup(name);
    func.description = strdup(description);
    func.parameter_count = param_count;
    func.execute_func = execute_func;

    if (func.name == NULL || func.description == NULL) {
        free(func.name);
        free(func.description);
        return -1;
    }

    if (param_count > 0 && parameters != NULL) {
        func.parameters = malloc(param_count * sizeof(ToolParameter));
        if (func.parameters == NULL) {
            free(func.name);
            free(func.description);
            return -1;
        }

        for (int i = 0; i < param_count; i++) {
            ToolParameter *src_param = &parameters[i];
            ToolParameter *dst_param = &func.parameters[i];

            dst_param->name = strdup(src_param->name);
            dst_param->type = strdup(src_param->type);
            dst_param->description = strdup(src_param->description);
            dst_param->required = src_param->required;
            dst_param->enum_count = src_param->enum_count;
            dst_param->items_schema = src_param->items_schema ? strdup(src_param->items_schema) : NULL;

            if (dst_param->name == NULL || dst_param->type == NULL || dst_param->description == NULL) {
                for (int j = 0; j <= i; j++) {
                    free(func.parameters[j].name);
                    free(func.parameters[j].type);
                    free(func.parameters[j].description);
                    free(func.parameters[j].items_schema);
                }
                free(func.parameters);
                free(func.name);
                free(func.description);
                return -1;
            }

            if (src_param->enum_count > 0 && src_param->enum_values != NULL) {
                dst_param->enum_values = malloc(src_param->enum_count * sizeof(char*));
                if (dst_param->enum_values == NULL) {
                    for (int j = 0; j <= i; j++) {
                        free(func.parameters[j].name);
                        free(func.parameters[j].type);
                        free(func.parameters[j].description);
                        free(func.parameters[j].items_schema);
                        if (j < i && func.parameters[j].enum_values != NULL) {
                            for (int k = 0; k < func.parameters[j].enum_count; k++) {
                                free(func.parameters[j].enum_values[k]);
                            }
                            free(func.parameters[j].enum_values);
                        }
                    }
                    free(func.parameters);
                    free(func.name);
                    free(func.description);
                    return -1;
                }

                for (int j = 0; j < src_param->enum_count; j++) {
                    dst_param->enum_values[j] = strdup(src_param->enum_values[j]);
                    if (dst_param->enum_values[j] == NULL) {
                        for (int k = 0; k < j; k++) {
                            free(dst_param->enum_values[k]);
                        }
                        free(dst_param->enum_values);
                        for (int k = 0; k <= i; k++) {
                            free(func.parameters[k].name);
                            free(func.parameters[k].type);
                            free(func.parameters[k].description);
                            free(func.parameters[k].items_schema);
                            if (k < i && func.parameters[k].enum_values != NULL) {
                                for (int l = 0; l < func.parameters[k].enum_count; l++) {
                                    free(func.parameters[k].enum_values[l]);
                                }
                                free(func.parameters[k].enum_values);
                            }
                        }
                        free(func.parameters);
                        free(func.name);
                        free(func.description);
                        return -1;
                    }
                }
            } else {
                dst_param->enum_values = NULL;
            }
        }
    } else {
        func.parameters = NULL;
    }

    if (ToolFunctionArray_push(&registry->functions, func) != 0) {
        free_tool_function_contents(&func);
        return -1;
    }
    return 0;
}

char* generate_tools_json(const ToolRegistry *registry) {
    return tool_format_openai.generate_tools_json(registry);
}

char* generate_anthropic_tools_json(const ToolRegistry *registry) {
    return tool_format_anthropic.generate_tools_json(registry);
}

int parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    return tool_format_openai.parse_tool_calls(json_response, tool_calls, call_count);
}

int parse_anthropic_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    return tool_format_anthropic.parse_tool_calls(json_response, tool_calls, call_count);
}

int execute_tool_call(const ToolRegistry *registry, const ToolCall *tool_call, ToolResult *result) {
    if (registry == NULL || tool_call == NULL || result == NULL) {
        return -1;
    }

    result->tool_call_id = strdup(tool_call->id);
    result->success = 0;
    result->result = NULL;

    if (result->tool_call_id == NULL) {
        return -1;
    }

    for (size_t i = 0; i < registry->functions.count; i++) {
        if (strcmp(registry->functions.data[i].name, tool_call->name) == 0) {
            return registry->functions.data[i].execute_func(tool_call, result);
        }
    }

    result->result = strdup("Error: Unknown tool");
    result->success = 0;

    if (result->result == NULL) {
        free(result->tool_call_id);
        return -1;
    }

    return 0;
}

char* generate_tool_results_json(const ToolResult *results, int result_count) {
    if (results == NULL || result_count <= 0) {
        return NULL;
    }

    size_t estimated_size = 1000 + (result_count * 500);
    char *json = malloc(estimated_size);
    if (json == NULL) {
        return NULL;
    }

    strcpy(json, "[");

    for (int i = 0; i < result_count; i++) {
        if (i > 0) {
            strcat(json, ", ");
        }

        strcat(json, "{\"role\": \"tool\", \"tool_call_id\": \"");
        strcat(json, results[i].tool_call_id);
        strcat(json, "\", \"content\": \"");

        const char *src = results[i].result;
        size_t current_len = strlen(json);
        char *dst = json + current_len;
        size_t remaining = estimated_size - current_len - 100; // Leave room for closing

        while (*src != '\0' && remaining > 2) {
            if (*src == '"') {
                *dst++ = '\\';
                *dst++ = '"';
                remaining -= 2;
            } else if (*src == '\\') {
                *dst++ = '\\';
                *dst++ = '\\';
                remaining -= 2;
            } else if (*src == '\n') {
                *dst++ = '\\';
                *dst++ = 'n';
                remaining -= 2;
            } else {
                *dst++ = *src;
                remaining--;
            }
            src++;
        }
        *dst = '\0';

        strcat(json, "\"}");
    }

    strcat(json, "]");

    return json;
}

char* generate_single_tool_message(const ToolResult *result) {
    if (result == NULL || result->tool_call_id == NULL || result->result == NULL) {
        return NULL;
    }

    size_t estimated_size = strlen(result->result) * 2 + 200; // *2 for escaping
    char *message = malloc(estimated_size);
    if (message == NULL) {
        return NULL;
    }

    snprintf(message, estimated_size, "Tool call %s result: %s",
             result->tool_call_id, result->result);

    return message;
}

void cleanup_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
        return;
    }

    for (size_t i = 0; i < registry->functions.count; i++) {
        free_tool_function_contents(&registry->functions.data[i]);
    }
    ToolFunctionArray_destroy(&registry->functions);
}

void cleanup_tool_calls(ToolCall *tool_calls, int call_count) {
    if (tool_calls == NULL) {
        return;
    }

    for (int i = 0; i < call_count; i++) {
        free(tool_calls[i].id);
        free(tool_calls[i].name);
        free(tool_calls[i].arguments);
    }

    free(tool_calls);
}

void cleanup_tool_results(ToolResult *results, int result_count) {
    if (results == NULL) {
        return;
    }

    for (int i = 0; i < result_count; i++) {
        free(results[i].tool_call_id);
        free(results[i].result);
    }

    free(results);
}
