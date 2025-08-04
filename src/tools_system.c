#include "tools_system.h"
#include "json_utils.h"
#include "shell_tool.h"
#include "file_tools.h"
#include "links_tool.h"
#include "todo_tool.h"
#include "output_formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

// Unescape JSON string in-place
static void unescape_json_string(char *str) {
    if (!str) return;
    
    char *src = str;
    char *dst = str;
    
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            switch (*(src + 1)) {
                case 'n':
                    *dst++ = '\n';
                    src += 2;
                    break;
                case 't':
                    *dst++ = '\t';
                    src += 2;
                    break;
                case 'r':
                    *dst++ = '\r';
                    src += 2;
                    break;
                case '\\':
                    *dst++ = '\\';
                    src += 2;
                    break;
                case '"':
                    *dst++ = '"';
                    src += 2;
                    break;
                default:
                    // Unknown escape, keep both characters
                    *dst++ = *src++;
                    *dst++ = *src++;
                    break;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

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
    
    // Calculate required size accurately
    size_t required_size = 2; // For "[" and "]"
    
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
        }
    }
    
    // Multiply by 2 for safety margin
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
        
        ret = snprintf(json + pos, required_size - pos, 
            "{\"type\": \"function\", \"function\": {\"name\": \"%s\", \"description\": \"%s\"",
            func->name, func->description);
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
                
                ret = snprintf(json + pos, required_size - pos, 
                    "\"%s\": {\"type\": \"%s\", \"description\": \"%s\"",
                    param->name, param->type, param->description);
                if (ret < 0 || (size_t)ret >= required_size - pos) {
                    free(json);
                    return NULL;
                }
                pos += ret;
                
                // Add items schema for array types
                if (strcmp(param->type, "array") == 0) {
                    ret = snprintf(json + pos, required_size - pos, ", \"items\": {\"type\": \"object\"}");
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
        
        // Anthropic uses a simpler format without the "type": "function" wrapper
        strcat(json, "{\"name\": \"");
        strcat(json, func->name);
        strcat(json, "\", \"description\": \"");
        strcat(json, func->description);
        strcat(json, "\", \"input_schema\": {\"type\": \"object\"");
        
        if (func->parameter_count > 0) {
            strcat(json, ", \"properties\": {");
            
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
                
                // Add items schema for array types
                if (strcmp(param->type, "array") == 0) {
                    strcat(json, ", \"items\": {\"type\": \"object\"}");
                }
                
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
            
            strcat(json, "}");
            
            // Add required array if there are required parameters
            int has_required = 0;
            for (int j = 0; j < func->parameter_count; j++) {
                if (func->parameters[j].required) {
                    has_required = 1;
                    break;
                }
            }
            
            if (has_required) {
                strcat(json, ", \"required\": [");
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
                strcat(json, "]");
            }
        }
        
        strcat(json, "}}");
    }
    
    strcat(json, "]");
    
    return json;
}

// Wrapper around unified JSON parser for tool calls
static char* extract_json_string(const char *json, const char *key) {
    JsonParser parser = {0};
    if (json_parser_init(&parser, json) != 0) {
        return NULL;
    }
    return json_parser_extract_string(&parser, key);
}

// Wrapper around unified JSON parser for tool objects
static char* extract_json_object(const char *json, const char *key) {
    JsonParser parser = {0};
    if (json_parser_init(&parser, json) != 0) {
        return NULL;
    }
    return json_parser_extract_object(&parser, key);
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
        
        memcpy(call_json, json_start, json_len);
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
            } else {
                // Unescape JSON string arguments
                unescape_json_string(call->arguments);
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
    
    // Count tool calls in the array
    int count = 0;
    const char *current = array_start + 1;
    while (*current != '\0' && *current != ']') {
        if (*current == '{') {
            count++;
            // Skip to the end of this object
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
    
    // Allocate array for all tool calls
    ToolCall *calls = malloc(count * sizeof(ToolCall));
    if (calls == NULL) {
        return -1;
    }
    
    // Parse each tool call
    current = array_start + 1;
    int parsed_count = 0;
    
    while (*current != '\0' && *current != ']' && parsed_count < count) {
        // Skip whitespace and commas
        while (*current == ' ' || *current == '\t' || *current == '\n' || *current == '\r' || *current == ',') {
            current++;
        }
        
        if (*current != '{') {
            current++;
            continue;
        }
        
        // Find the end of this tool call object
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
            // Cleanup and return error
            for (int i = 0; i < parsed_count; i++) {
                free(calls[i].id);
                free(calls[i].name);
                free(calls[i].arguments);
            }
            free(calls);
            return -1;
        }
        
        // Extract the tool call JSON
        size_t call_len = call_end - call_start;
        char *call_json = malloc(call_len + 1);
        if (call_json == NULL) {
            // Cleanup and return error
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
        
        // Parse the tool call
        ToolCall *call = &calls[parsed_count];
        call->id = extract_json_string(call_json, "id");
        call->name = NULL;
        call->arguments = NULL;
        
        // Extract function name and arguments from nested function object
        char *function_obj = extract_json_object(call_json, "function");
        if (function_obj != NULL) {
            call->name = extract_json_string(function_obj, "name");
            call->arguments = extract_json_string(function_obj, "arguments");
            if (call->arguments != NULL) {
                // Unescape JSON string arguments
                unescape_json_string(call->arguments);
            }
            free(function_obj);
        }
        
        free(call_json);
        
        // Validate the parsed call
        if (call->id == NULL || call->name == NULL) {
            // Cleanup and return error
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
        
        // Default empty arguments if not provided
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
    
    // Anthropic returns tool use in a specific format in the content array
    // Look for "type": "tool_use" in the content array
    const char *content_array = strstr(json_response, "\"content\":");
    if (content_array == NULL) {
        return 0; // No content array, no tool calls
    }
    
    // Count tool_use items in the content array
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
    
    // Parse each tool_use item
    search_pos = content_array;
    int parsed_count = 0;
    
    while (parsed_count < tool_count) {
        // Find next tool_use
        const char *tool_use = strstr(search_pos, "\"type\": \"tool_use\"");
        if (tool_use == NULL) {
            tool_use = strstr(search_pos, "\"type\":\"tool_use\"");
        }
        if (tool_use == NULL) {
            break;
        }
        
        // Find the containing object boundaries
        const char *obj_start = tool_use;
        while (obj_start > content_array && *obj_start != '{') {
            obj_start--;
        }
        
        if (*obj_start != '{') {
            search_pos = tool_use + 1;
            continue;
        }
        
        // Find matching closing brace
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
        
        // Extract the tool use object
        size_t obj_len = obj_end - obj_start;
        char *tool_obj = malloc(obj_len + 1);
        if (tool_obj == NULL) {
            cleanup_tool_calls(*tool_calls, parsed_count);
            *tool_calls = NULL;
            return -1;
        }
        
        memcpy(tool_obj, obj_start, obj_len);
        tool_obj[obj_len] = '\0';
        
        // Parse the tool use object
        ToolCall *call = &(*tool_calls)[parsed_count];
        call->id = extract_json_string(tool_obj, "id");
        call->name = extract_json_string(tool_obj, "name");
        call->arguments = extract_json_object(tool_obj, "input");
        
        // If arguments is not an object, try string format
        if (call->arguments == NULL) {
            call->arguments = extract_json_string(tool_obj, "input");
            if (call->arguments == NULL) {
                call->arguments = strdup("{}");
            } else {
                // Unescape JSON string arguments
                unescape_json_string(call->arguments);
            }
        }
        
        free(tool_obj);
        
        if (call->id == NULL || call->name == NULL) {
            // Generate ID if missing
            if (call->id == NULL) {
                char id_buf[32];
                snprintf(id_buf, sizeof(id_buf), "anthropic_call_%d", parsed_count);
                call->id = strdup(id_buf);
            }
            
            // Skip if name is missing
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

// Demo tool implementations (removed - users should implement their own tools)
// These were placeholder implementations for testing
// Users should implement their own tool execution logic in execute_tool_call


// Truncate a string for display, adding "..." if truncated


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
    
    // Execute the tool - we'll log everything at the end
    int exec_result = 0;
    
    // Look for the tool in the registry
    for (int i = 0; i < registry->function_count; i++) {
        if (strcmp(registry->functions[i].name, tool_call->name) == 0) {
            // Handle specific tool implementations
            if (strcmp(tool_call->name, "shell_execute") == 0) {
                exec_result = execute_shell_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_read") == 0) {
                exec_result = execute_file_read_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_write") == 0) {
                exec_result = execute_file_write_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_append") == 0) {
                exec_result = execute_file_append_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_list") == 0) {
                exec_result = execute_file_list_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_search") == 0) {
                exec_result = execute_file_search_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_info") == 0) {
                exec_result = execute_file_info_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "file_delta") == 0) {
                exec_result = execute_file_delta_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "web_fetch") == 0) {
                exec_result = execute_links_tool_call(tool_call, result);
            } else if (strcmp(tool_call->name, "TodoWrite") == 0) {
                exec_result = execute_todo_tool_call(tool_call, result);
            } else {
                // Tool found in registry but no implementation provided
                result->result = strdup("Error: Tool execution not implemented");
                result->success = 0;
                exec_result = 0;
            }
            
            // Log the result after execution with improved formatting
            log_tool_execution_improved(tool_call->name, tool_call->arguments, result->success, result->result);
            return exec_result;
        }
    }
    
    // Tool not found in registry
    result->result = strdup("Error: Unknown tool");
    result->success = 0;
    
    if (result->result == NULL) {
        free(result->tool_call_id);
        return -1;
    }
    
    // Log the result after execution with improved formatting
    log_tool_execution_improved(tool_call->name, tool_call->arguments, result->success, result->result);
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

// Generate a single tool result message for conversation history
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
    
    // Register all built-in tools that are compiled into the binary
    if (register_shell_tool(registry) != 0) {
        return -1;
    }
    
    // Register file manipulation tools
    if (register_file_tools(registry) != 0) {
        return -1;
    }
    
    // Register Links web browser tool
    if (register_links_tool(registry) != 0) {
        return -1;
    }
    
    // Future built-in tools would be registered here:
    // if (register_git_tool(registry) != 0) return -1;
    
    return 0;
}

int load_tools_config(ToolRegistry *registry, const char *config_file) {
    if (registry == NULL) {
        return -1;
    }
    
    // Try to load from configuration file for user-defined custom tools
    FILE *file = fopen(config_file, "r");
    if (file == NULL) {
        // Configuration file doesn't exist - this is OK, no custom tools
        return 0;
    }
    
    fclose(file);
    
    // For now, return 0 (no custom tools loaded) since config file parsing is not implemented
    // Users can extend this function to parse JSON/YAML configuration files
    // and register their own custom tools using register_tool() or similar functions
    
    return 0;
}

void cleanup_tool_registry(ToolRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    
    // Defensive programming: check if function_count is reasonable first
    // before trying to access the functions pointer
    if (registry->function_count < 0 || registry->function_count > 1000) {
        // Function count is garbage - registry is corrupted
        return;
    }
    
    if (registry->function_count == 0) {
        // No functions to clean up, but still clean up the functions array if allocated
        free(registry->functions);
        registry->functions = NULL;
        return;
    }
    
    if (registry->functions == NULL) {
        // Functions pointer is NULL but count > 0 - corrupted
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