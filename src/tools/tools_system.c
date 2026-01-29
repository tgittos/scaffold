#include "tools_system.h"
#include <cJSON.h>
#include "todo_tool.h"
#include "vector_db_tool.h"
#include "memory_tool.h"
#include "pdf_tool.h"
#include "python_tool.h"
#include "python_tool_files.h"
#include "output_formatter.h"
#include "json_escape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void init_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    
    registry->functions = NULL;
    registry->function_count = 0;
}

int register_tool(ToolRegistry *registry, const char *name, const char *description, 
                  ToolParameter *parameters, int param_count, tool_execute_func_t execute_func) {
    if (registry == NULL || name == NULL || description == NULL || execute_func == NULL) {
        return -1;
    }
    
    ToolFunction *new_functions = realloc(registry->functions,
                                          (registry->function_count + 1) * sizeof(ToolFunction));
    if (new_functions == NULL) {
        return -1;
    }
    
    registry->functions = new_functions;
    ToolFunction *func = &registry->functions[registry->function_count];
    
    func->name = strdup(name);
    func->description = strdup(description);
    func->parameter_count = param_count;
    func->execute_func = execute_func;
    
    if (func->name == NULL || func->description == NULL) {
        free(func->name);
        free(func->description);
        return -1;
    }
    
    if (param_count > 0 && parameters != NULL) {
        func->parameters = malloc(param_count * sizeof(ToolParameter));
        if (func->parameters == NULL) {
            free(func->name);
            free(func->description);
            return -1;
        }
        

        for (int i = 0; i < param_count; i++) {
            ToolParameter *src_param = &parameters[i];
            ToolParameter *dst_param = &func->parameters[i];
            
            dst_param->name = strdup(src_param->name);
            dst_param->type = strdup(src_param->type);
            dst_param->description = strdup(src_param->description);
            dst_param->required = src_param->required;
            dst_param->enum_count = src_param->enum_count;
            dst_param->items_schema = src_param->items_schema ? strdup(src_param->items_schema) : NULL;

            if (dst_param->name == NULL || dst_param->type == NULL || dst_param->description == NULL) {

                for (int j = 0; j <= i; j++) {
                    free(func->parameters[j].name);
                    free(func->parameters[j].type);
                    free(func->parameters[j].description);
                }
                free(func->parameters);
                free(func->name);
                free(func->description);
                return -1;
            }
            

            if (src_param->enum_count > 0 && src_param->enum_values != NULL) {
                dst_param->enum_values = malloc(src_param->enum_count * sizeof(char*));
                if (dst_param->enum_values == NULL) {
    
                    for (int j = 0; j <= i; j++) {
                        free(func->parameters[j].name);
                        free(func->parameters[j].type);
                        free(func->parameters[j].description);
                        if (j < i && func->parameters[j].enum_values != NULL) {
                            for (int k = 0; k < func->parameters[j].enum_count; k++) {
                                free(func->parameters[j].enum_values[k]);
                            }
                            free(func->parameters[j].enum_values);
                        }
                    }
                    free(func->parameters);
                    free(func->name);
                    free(func->description);
                    return -1;
                }
                
                for (int j = 0; j < src_param->enum_count; j++) {
                    dst_param->enum_values[j] = strdup(src_param->enum_values[j]);
                    if (dst_param->enum_values[j] == NULL) {
        
                        for (int k = 0; k < j; k++) {
                            free(dst_param->enum_values[k]);
                        }
                        free(dst_param->enum_values);
                        // Clean up previously completed parameters
                        for (int k = 0; k <= i; k++) {
                            free(func->parameters[k].name);
                            free(func->parameters[k].type);
                            free(func->parameters[k].description);
                            if (k < i && func->parameters[k].enum_values != NULL) {
                                for (int l = 0; l < func->parameters[k].enum_count; l++) {
                                    free(func->parameters[k].enum_values[l]);
                                }
                                free(func->parameters[k].enum_values);
                            }
                        }
                        free(func->parameters);
                        free(func->name);
                        free(func->description);
                        return -1;
                    }
                }
            } else {
                dst_param->enum_values = NULL;
            }
        }
    } else {
        func->parameters = NULL;
    }
    
    registry->function_count++;
    return 0;
}

char* generate_tools_json(const ToolRegistry *registry) {
    if (registry == NULL || registry->function_count == 0) {
        return NULL;
    }
    
    size_t required_size = 2;
    
    for (int i = 0; i < registry->function_count; i++) {
        const ToolFunction *func = &registry->functions[i];
        required_size += strlen(func->name) + strlen(func->description) + 200; // Base structure + safety margin
        
        if (i > 0) required_size += 2; // ", "
        
        for (int j = 0; j < func->parameter_count; j++) {
            const ToolParameter *param = &func->parameters[j];
            required_size += strlen(param->name) + strlen(param->type) + strlen(param->description) + 100;

            if (param->enum_values != NULL) {
                for (int k = 0; k < param->enum_count; k++) {
                    required_size += strlen(param->enum_values[k]) + 10; // quotes, comma, safety
                }
            }

            if (param->items_schema != NULL) {
                required_size += strlen(param->items_schema) + 20;
            }
        }
    }
    
    required_size *= 2;  // Safety margin for JSON escaping overhead
    
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
    
    for (int i = 0; i < registry->function_count; i++) {
        const ToolFunction *func = &registry->functions[i];
        
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

char* generate_anthropic_tools_json(const ToolRegistry *registry) {
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

static char* extract_string_from_json(const char *json, const char *key) {
    cJSON *json_obj = cJSON_Parse(json);
    if (json_obj == NULL) {
        return NULL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json_obj, key);
    char *result = NULL;
    if (cJSON_IsString(item)) {
        result = strdup(cJSON_GetStringValue(item));
    }
    
    cJSON_Delete(json_obj);
    return result;
}

static char* extract_object_from_json(const char *json, const char *key) {
    cJSON *json_obj = cJSON_Parse(json);
    if (json_obj == NULL) {
        return NULL;
    }
    
    cJSON *item = cJSON_GetObjectItem(json_obj, key);
    char *result = NULL;
    if (item != NULL) {
        result = cJSON_PrintUnformatted(item);
    }
    
    cJSON_Delete(json_obj);
    return result;
}

int parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    if (json_response == NULL || tool_calls == NULL || call_count == NULL) {
        return -1;
    }
    
    *tool_calls = NULL;
    *call_count = 0;
    
    const char *tool_calls_start = strstr(json_response, "\"tool_calls\":");
    if (tool_calls_start == NULL) {
        // Fallback: look for custom <tool_call> XML-style format
        const char *custom_call_start = strstr(json_response, "<tool_call>");
        if (custom_call_start == NULL) {
            return 0; // No tool calls found - this is OK
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
        call->name = extract_string_from_json(call_json, "name");
        call->arguments = extract_object_from_json(call_json, "arguments");
        
        if (call->arguments == NULL) {
            call->arguments = extract_string_from_json(call_json, "arguments");
            if (call->arguments == NULL) {
                call->arguments = strdup("{}");
            }
            // Note: cJSON already handles JSON string unescaping during parsing,
            // so we don't need to call unescape_json_string here
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
        return 0; // Empty array
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
        call->id = extract_string_from_json(call_json, "id");
        call->name = NULL;
        call->arguments = NULL;
        
        char *function_obj = extract_object_from_json(call_json, "function");
        if (function_obj != NULL) {
            call->name = extract_string_from_json(function_obj, "name");
            call->arguments = extract_string_from_json(function_obj, "arguments");
            // Note: cJSON already handles JSON string unescaping during parsing,
            // so we don't need to call unescape_json_string here
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

int parse_anthropic_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    if (json_response == NULL || tool_calls == NULL || call_count == NULL) {
        return -1;
    }
    
    *tool_calls = NULL;
    *call_count = 0;
    
    const char *content_array = strstr(json_response, "\"content\":");
    if (content_array == NULL) {
        return 0; // No content array, no tool calls
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
        return 0; // No tool calls found
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
        call->id = extract_string_from_json(tool_obj, "id");
        call->name = extract_string_from_json(tool_obj, "name");
        call->arguments = extract_object_from_json(tool_obj, "input");
        
        if (call->arguments == NULL) {
            call->arguments = extract_string_from_json(tool_obj, "input");
            if (call->arguments == NULL) {
                call->arguments = strdup("{}");
            }
            // Note: cJSON already handles JSON string unescaping during parsing,
            // so we don't need to call unescape_json_string here
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

int execute_tool_call(const ToolRegistry *registry, const ToolCall *tool_call, ToolResult *result) {
    if (registry == NULL || tool_call == NULL || result == NULL) {
        return -1;
    }
    
    result->tool_call_id = strdup(tool_call->id);
    result->success = 0; // Default to failure
    result->result = NULL;
    
    if (result->tool_call_id == NULL) {
        return -1;
    }
    
    for (int i = 0; i < registry->function_count; i++) {
        if (strcmp(registry->functions[i].name, tool_call->name) == 0) {
            return registry->functions[i].execute_func(tool_call, result);
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

int register_builtin_tools(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }

    if (register_vector_db_tool(registry) != 0) {
        return -1;
    }

    if (register_memory_tools(registry) != 0) {
        return -1;
    }

    if (register_pdf_tool(registry) != 0) {
        return -1;
    }

    if (register_python_tool(registry) != 0) {
        return -1;
    }

    // Eagerly init Python so tool files from ~/.local/ralph/tools/ can be loaded
    if (python_interpreter_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize Python interpreter\n");
    }

    // Python file tools (read_file, write_file, shell, etc.) provide external system access
    if (python_register_tool_schemas(registry) != 0) {
        fprintf(stderr, "Warning: Failed to register Python file tools\n");
    }

    return 0;
}

void cleanup_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    
    // Guard against corrupted registry state
    if (registry->function_count < 0 || registry->function_count > 1000) {
        return;
    }

    if (registry->function_count == 0) {
        free(registry->functions);
        registry->functions = NULL;
        return;
    }

    if (registry->functions == NULL) {
        registry->function_count = 0;
        return;
    }
    
    for (int i = 0; i < registry->function_count; i++) {
        ToolFunction *func = &registry->functions[i];
        
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
    
    free(registry->functions);
    registry->functions = NULL;
    registry->function_count = 0;
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