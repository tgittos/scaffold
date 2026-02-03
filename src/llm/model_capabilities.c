#include "model_capabilities.h"
#include "ui/output_formatter.h"
#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

PTRARRAY_DEFINE(ModelRegistry, ModelCapabilities)

static int model_pattern_match(const char* model_name, const char* pattern) {
    if (!model_name || !pattern) {
        return 0;
    }
    
    size_t model_len = strlen(model_name);
    size_t pattern_len = strlen(pattern);
    
    char* model_lower = malloc(model_len + 1);
    char* pattern_lower = malloc(pattern_len + 1);
    
    if (!model_lower || !pattern_lower) {
        free(model_lower);
        free(pattern_lower);
        return 0;
    }
    
    for (size_t i = 0; i <= model_len; i++) {
        model_lower[i] = tolower(model_name[i]);
    }
    
    for (size_t i = 0; i <= pattern_len; i++) {
        pattern_lower[i] = tolower(pattern[i]);
    }
    
    int result = strstr(model_lower, pattern_lower) != NULL;
    
    free(model_lower);
    free(pattern_lower);
    
    return result;
}

int init_model_registry(ModelRegistry* registry) {
    // Models are static, so we don't own them - pass NULL destructor
    return ModelRegistry_init_capacity(registry, 16, NULL);
}

int register_model_capabilities(ModelRegistry* registry, ModelCapabilities* model) {
    return ModelRegistry_push(registry, model);
}

ModelCapabilities* detect_model_capabilities(ModelRegistry* registry, const char* model_name) {
    if (!registry || !model_name) {
        return NULL;
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (model_pattern_match(model_name, registry->data[i]->model_pattern)) {
            return registry->data[i];
        }
    }

    for (size_t i = 0; i < registry->count; i++) {
        if (strcmp(registry->data[i]->model_pattern, "default") == 0) {
            return registry->data[i];
        }
    }

    return NULL;
}

void cleanup_model_registry(ModelRegistry* registry) {
    ModelRegistry_destroy(registry);
}

char* generate_model_tools_json(ModelRegistry* registry, const char* model_name, const ToolRegistry* tools) {
    if (!registry || !tools) {
        return NULL;
    }
    
    ModelCapabilities* model = detect_model_capabilities(registry, model_name);
    if (!model || !model->generate_tools_json) {
        return NULL;
    }
    
    return model->generate_tools_json(tools);
}

int parse_model_tool_calls(ModelRegistry* registry, const char* model_name, 
                          const char* json_response, ToolCall** tool_calls, int* call_count) {
    if (!registry || !json_response || !tool_calls || !call_count) {
        return -1;
    }
    
    ModelCapabilities* model = detect_model_capabilities(registry, model_name);
    if (!model || !model->parse_tool_calls) {
        return -1;
    }
    
    return model->parse_tool_calls(json_response, tool_calls, call_count);
}

char* format_model_tool_result_message(ModelRegistry* registry, const char* model_name, const ToolResult* result) {
    if (!registry || !result) {
        return NULL;
    }
    
    ModelCapabilities* model = detect_model_capabilities(registry, model_name);
    if (!model || !model->format_tool_result_message) {
        return NULL;
    }
    
    return model->format_tool_result_message(result);
}

char* format_model_assistant_tool_message(ModelRegistry* registry, const char* model_name,
                                         const char* response_content, const ToolCall* tool_calls, int tool_call_count) {
    if (!registry) {
        return NULL;
    }
    
    ModelCapabilities* model = detect_model_capabilities(registry, model_name);
    if (!model || !model->format_assistant_tool_message) {
        return NULL;
    }
    
    return model->format_assistant_tool_message(response_content, tool_calls, tool_call_count);
}