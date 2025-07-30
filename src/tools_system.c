#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

void init_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    
    registry->functions = NULL;
    registry->function_count = 0;
}

// register_demo_tool function removed - users should implement their own tool registration
// Users can extend this file with their own register_tool function to add custom tools

char* generate_tools_json(const ToolRegistry *registry) {
    if (registry == NULL || registry->function_count == 0) {
        return NULL;
    }
    
    // Estimate size needed
    size_t estimated_size = 1000 + (registry->function_count * 500);
    char *json = malloc(estimated_size);
    if (json == NULL) {
        return NULL;
    }
    
    strcpy(json, "[");
    
    for (int i = 0; i < registry->function_count; i++) {
        const ToolFunction *func = &registry->functions[i];
        
        if (i > 0) {
            strcat(json, ", ");
        }
        
        strcat(json, "{\"type\": \"function\", \"function\": {");
        strcat(json, "\"name\": \"");
        strcat(json, func->name);
        strcat(json, "\", \"description\": \"");
        strcat(json, func->description);
        strcat(json, "\"");
        
        if (func->parameter_count > 0) {
            strcat(json, ", \"parameters\": {\"type\": \"object\", \"properties\": {");
            
            for (int j = 0; j < func->parameter_count; j++) {
                const ToolParameter *param = &func->parameters[j];
                
                if (j > 0) {
                    strcat(json, ", ");
                }
                
                strcat(json, "\"");
                strcat(json, param->name);
                strcat(json, "\": {\"type\": \"");
                strcat(json, param->type);
                strcat(json, "\", \"description\": \"");
                strcat(json, param->description);
                strcat(json, "\"");
                
                if (param->enum_values != NULL && param->enum_count > 0) {
                    strcat(json, ", \"enum\": [");
                    for (int k = 0; k < param->enum_count; k++) {
                        if (k > 0) strcat(json, ", ");
                        strcat(json, "\"");
                        strcat(json, param->enum_values[k]);
                        strcat(json, "\"");
                    }
                    strcat(json, "]");
                }
                
                strcat(json, "}");
            }
            
            strcat(json, "}, \"required\": [");
            int first_required = 1;
            for (int j = 0; j < func->parameter_count; j++) {
                if (func->parameters[j].required) {
                    if (!first_required) {
                        strcat(json, ", ");
                    }
                    strcat(json, "\"");
                    strcat(json, func->parameters[j].name);
                    strcat(json, "\"");
                    first_required = 0;
                }
            }
            strcat(json, "]}");
        }
        
        strcat(json, "}}");
    }
    
    strcat(json, "]");
    
    return json;
}

// Simple JSON parser for tool calls (basic implementation)
static char* extract_json_string(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_key);
    
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    
    if (*start != '"') {
        return NULL; // Not a string value
    }
    
    start++; // Skip opening quote
    const char *end = start;
    
    // Find closing quote (simple - doesn't handle escapes properly)
    while (*end != '\0' && *end != '"') {
        end++;
    }
    
    if (*end != '"') {
        return NULL;
    }
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }
    
    strncpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

// Extract JSON object as string
static char* extract_json_object(const char *json, const char *key) {
    char search_key[256];
    snprintf(search_key, sizeof(search_key), "\"%s\":", key);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_key);
    
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    
    if (*start != '{') {
        return NULL; // Not an object
    }
    
    const char *end = start + 1;
    int brace_count = 1;
    
    // Find matching closing brace
    while (*end != '\0' && brace_count > 0) {
        if (*end == '{') {
            brace_count++;
        } else if (*end == '}') {
            brace_count--;
        }
        end++;
    }
    
    if (brace_count != 0) {
        return NULL;
    }
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) {
        return NULL;
    }
    
    strncpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

int parse_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count) {
    if (json_response == NULL || tool_calls == NULL || call_count == NULL) {
        return -1;
    }
    
    *tool_calls = NULL;
    *call_count = 0;
    
    // First try to find standard tool_calls array
    const char *tool_calls_start = strstr(json_response, "\"tool_calls\":");
    if (tool_calls_start == NULL) {
        // If no standard format, look for custom <tool_call> format
        const char *custom_call_start = strstr(json_response, "<tool_call>");
        if (custom_call_start == NULL) {
            return 0; // No tool calls found - this is OK
        }
        
        // Parse custom format: <tool_call>{"name": "...", "arguments": {...}}</tool_call>
        const char *json_start = custom_call_start + strlen("<tool_call>");
        const char *json_end = strstr(json_start, "</tool_call>");
        if (json_end == NULL) {
            return -1;
        }
        
        // Skip whitespace and newlines
        while (*json_start == ' ' || *json_start == '\t' || *json_start == '\n' || *json_start == '\r') {
            json_start++;
        }
        
        size_t json_len = json_end - json_start;
        char *call_json = malloc(json_len + 1);
        if (call_json == NULL) {
            return -1;
        }
        
        strncpy(call_json, json_start, json_len);
        call_json[json_len] = '\0';
        
        // Parse the tool call JSON
        ToolCall *call = malloc(sizeof(ToolCall));
        if (call == NULL) {
            free(call_json);
            return -1;
        }
        
        call->id = strdup("custom_call_1"); // Generate an ID
        call->name = extract_json_string(call_json, "name");
        call->arguments = extract_json_object(call_json, "arguments");
        
        // If arguments is not an object, try string format
        if (call->arguments == NULL) {
            call->arguments = extract_json_string(call_json, "arguments");
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
    
    // Find the opening bracket of the array
    const char *array_start = strchr(tool_calls_start, '[');
    if (array_start == NULL) {
        return -1;
    }
    
    // For simplicity, we'll just parse the first tool call
    // A full implementation would parse the entire array
    const char *first_call = strchr(array_start, '{');
    if (first_call == NULL) {
        return 0; // Empty array
    }
    
    // Find the end of the first tool call object
    const char *call_end = first_call + 1;
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
        return -1;
    }
    
    // Extract the tool call JSON
    size_t call_len = call_end - first_call;
    char *call_json = malloc(call_len + 1);
    if (call_json == NULL) {
        return -1;
    }
    
    strncpy(call_json, first_call, call_len);
    call_json[call_len] = '\0';
    
    // Parse the tool call
    ToolCall *call = malloc(sizeof(ToolCall));
    if (call == NULL) {
        free(call_json);
        return -1;
    }
    
    call->id = extract_json_string(call_json, "id");
    call->arguments = extract_json_object(call_json, "arguments");
    
    // Extract function name from nested function object
    char *function_obj = extract_json_object(call_json, "function");
    if (function_obj != NULL) {
        call->name = extract_json_string(function_obj, "name");
        // Also extract arguments from the function object
        if (call->arguments == NULL) {
            call->arguments = extract_json_string(function_obj, "arguments");
        }
        free(function_obj);
    } else {
        call->name = NULL;
    }
    
    free(call_json);
    
    if (call->id == NULL || call->name == NULL) {
        free(call->id);
        free(call->name);
        free(call->arguments);
        free(call);
        return -1;
    }
    
    *tool_calls = call;
    *call_count = 1;
    
    return 0;
}

// Demo tool implementations (removed - users should implement their own tools)
// These were placeholder implementations for testing
// Users should implement their own tool execution logic in execute_tool_call

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
    
    // Look for the tool in the registry
    for (int i = 0; i < registry->function_count; i++) {
        if (strcmp(registry->functions[i].name, tool_call->name) == 0) {
            // Tool found in registry but no implementation provided
            // Users should extend this function to implement their own tool execution logic
            result->result = strdup("Error: Tool execution not implemented");
            result->success = 0;
            return 0;
        }
    }
    
    // Tool not found in registry
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
        
        // Escape quotes in result content
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

int load_tools_config(ToolRegistry *registry, const char *config_file) {
    if (registry == NULL) {
        return -1;
    }
    
    // Try to load from configuration file
    FILE *file = fopen(config_file, "r");
    if (file == NULL) {
        // Configuration file doesn't exist - this is OK, just have no tools
        return 0;
    }
    
    fclose(file);
    
    // For now, return 0 (no tools loaded) since config file parsing is not implemented
    // Users can extend this function to parse JSON/YAML configuration files
    // and register their own tools using register_tool() or similar functions
    
    return 0;
}

void cleanup_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
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