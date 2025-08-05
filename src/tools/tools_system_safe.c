// Safe replacement for tools_system.c buffer overflow functions
#include "tools_system.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// Safe tools JSON generation
char* generate_tools_json_safe(const ToolRegistry *registry) {
    if (registry == NULL || registry->function_count == 0) {
        char *empty = malloc(3);
        if (empty) strcpy(empty, "[]");
        return empty;
    }
    
    cJSON* tools_array = cJSON_CreateArray();
    if (!tools_array) {
        return NULL;
    }
    
    for (int i = 0; i < registry->function_count; i++) {
        const ToolFunction *func = &registry->functions[i];
        
        cJSON* tool = cJSON_CreateObject();
        if (!tool) {
            cJSON_Delete(tools_array);
            return NULL;
        }
        
        cJSON_AddStringToObject(tool, "type", "function");
        
        cJSON* function_obj = cJSON_CreateObject();
        if (!function_obj) {
            cJSON_Delete(tool);
            cJSON_Delete(tools_array);
            return NULL;
        }
        
        cJSON_AddStringToObject(function_obj, "name", func->name);
        cJSON_AddStringToObject(function_obj, "description", func->description);
        
        if (func->parameter_count > 0) {
            cJSON* parameters = cJSON_CreateObject();
            cJSON* properties = cJSON_CreateObject();
            cJSON* required = cJSON_CreateArray();
            
            if (!parameters || !properties || !required) {
                cJSON_Delete(tool);
                cJSON_Delete(function_obj);
                cJSON_Delete(tools_array);
                cJSON_Delete(parameters);
                cJSON_Delete(properties);
                cJSON_Delete(required);
                return NULL;
            }
            
            cJSON_AddStringToObject(parameters, "type", "object");
            
            for (int j = 0; j < func->parameter_count; j++) {
                const ToolParameter *param = &func->parameters[j];
                
                cJSON* param_obj = cJSON_CreateObject();
                if (param_obj) {
                    cJSON_AddStringToObject(param_obj, "type", param->type);
                    cJSON_AddStringToObject(param_obj, "description", param->description);
                    
                    if (strcmp(param->type, "array") == 0) {
                        cJSON* items = cJSON_CreateObject();
                        if (items) {
                            cJSON_AddStringToObject(items, "type", "object");
                            cJSON_AddItemToObject(param_obj, "items", items);
                        }
                    }
                    
                    if (param->enum_values != NULL && param->enum_count > 0) {
                        cJSON* enum_array = cJSON_CreateArray();
                        if (enum_array) {
                            for (int k = 0; k < param->enum_count; k++) {
                                cJSON* enum_val = cJSON_CreateString(param->enum_values[k]);
                                if (enum_val) {
                                    cJSON_AddItemToArray(enum_array, enum_val);
                                }
                            }
                            cJSON_AddItemToObject(param_obj, "enum", enum_array);
                        }
                    }
                    
                    cJSON_AddItemToObject(properties, param->name, param_obj);
                    
                    if (param->required) {
                        cJSON* req_name = cJSON_CreateString(param->name);
                        if (req_name) {
                            cJSON_AddItemToArray(required, req_name);
                        }
                    }
                }
            }
            
            cJSON_AddItemToObject(parameters, "properties", properties);
            cJSON_AddItemToObject(parameters, "required", required);
            cJSON_AddItemToObject(function_obj, "parameters", parameters);
        }
        
        cJSON_AddItemToObject(tool, "function", function_obj);
        cJSON_AddItemToArray(tools_array, tool);
    }
    
    char* json_string = cJSON_PrintUnformatted(tools_array);
    cJSON_Delete(tools_array);
    
    return json_string;
}