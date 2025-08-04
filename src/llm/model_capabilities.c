#include "model_capabilities.h"
#include "output_formatter.h"
#include "tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Helper function to perform case-insensitive pattern matching
static int model_pattern_match(const char* model_name, const char* pattern) {
    if (!model_name || !pattern) {
        return 0;
    }
    
    // Convert both to lowercase for comparison
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
    if (!registry) {
        return -1;
    }
    
    registry->capacity = 16;
    registry->count = 0;
    registry->models = calloc(registry->capacity, sizeof(ModelCapabilities*));
    
    if (!registry->models) {
        return -1;
    }
    
    return 0;
}

int register_model_capabilities(ModelRegistry* registry, ModelCapabilities* model) {
    if (!registry || !model) {
        return -1;
    }
    
    // Expand capacity if needed
    if (registry->count >= registry->capacity) {
        int new_capacity = registry->capacity * 2;
        ModelCapabilities** new_models = realloc(registry->models, 
                                                new_capacity * sizeof(ModelCapabilities*));
        if (!new_models) {
            return -1;
        }
        registry->models = new_models;
        registry->capacity = new_capacity;
    }
    
    registry->models[registry->count++] = model;
    return 0;
}

ModelCapabilities* detect_model_capabilities(ModelRegistry* registry, const char* model_name) {
    if (!registry || !model_name) {
        return NULL;
    }
    
    // Search for matching model
    for (int i = 0; i < registry->count; i++) {
        if (model_pattern_match(model_name, registry->models[i]->model_pattern)) {
            return registry->models[i];
        }
    }
    
    // Return default model if no match found
    for (int i = 0; i < registry->count; i++) {
        if (strcmp(registry->models[i]->model_pattern, "default") == 0) {
            return registry->models[i];
        }
    }
    
    return NULL;
}

void cleanup_model_registry(ModelRegistry* registry) {
    if (!registry) {
        return;
    }
    
    if (registry->models) {
        // Note: We don't free individual models as they're typically static
        free(registry->models);
        registry->models = NULL;
    }
    
    registry->count = 0;
    registry->capacity = 0;
}

int process_model_response(ModelRegistry* registry, const char* model_name, 
                          const char* content, ParsedResponse* result) {
    if (!registry || !content || !result) {
        return -1;
    }
    
    ModelCapabilities* model = detect_model_capabilities(registry, model_name);
    if (!model) {
        return -1;
    }
    
    if (model->process_response) {
        return model->process_response(content, result);
    }
    
    // Default processing if no custom processor
    return -1;
}

char* generate_model_tools_json(ModelRegistry* registry, const char* model_name, const ToolRegistry* tools) {
    if (!registry || !tools) {
        return NULL;
    }
    
    ModelCapabilities* model = detect_model_capabilities(registry, model_name);
    if (!model || !model->generate_tools_json) {
        // Fall back to default implementation
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