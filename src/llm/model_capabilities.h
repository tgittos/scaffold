#ifndef MODEL_CAPABILITIES_H
#define MODEL_CAPABILITIES_H

#include <stddef.h>

// Forward declaration - include the actual header instead
#include "output_formatter.h"
#include "tools_system.h"

// Model-specific capabilities
typedef struct {
    const char* model_pattern;  // Pattern to match model name (e.g., "qwen", "deepseek", "claude")
    
    // Thinking support
    int supports_thinking_tags;  // Whether model uses <think> tags
    const char* thinking_start_tag;
    const char* thinking_end_tag;
    
    // Response processing
    int (*process_response)(const char* content, ParsedResponse* result);
    
    // Tool/Function calling support
    int supports_function_calling;
    char* (*generate_tools_json)(const ToolRegistry* registry);
    int (*parse_tool_calls)(const char* json_response, ToolCall** tool_calls, int* call_count);
    char* (*format_tool_result_message)(const ToolResult* result);
    char* (*format_assistant_tool_message)(const char* response_content, const ToolCall* tool_calls, int tool_call_count);
    
    // Other model-specific features
    int max_context_length;
} ModelCapabilities;

// Model registry
typedef struct {
    ModelCapabilities** models;
    int count;
    int capacity;
} ModelRegistry;

// Core model management functions
int init_model_registry(ModelRegistry* registry);
int register_model_capabilities(ModelRegistry* registry, ModelCapabilities* model);
ModelCapabilities* detect_model_capabilities(ModelRegistry* registry, const char* model_name);
void cleanup_model_registry(ModelRegistry* registry);

// Built-in model registration functions
int register_qwen_models(ModelRegistry* registry);
int register_deepseek_models(ModelRegistry* registry);
int register_claude_models(ModelRegistry* registry);
int register_gpt_models(ModelRegistry* registry);
int register_default_model(ModelRegistry* registry);

// Helper function to process response with model-specific handling
int process_model_response(ModelRegistry* registry, const char* model_name, 
                          const char* content, ParsedResponse* result);

// Helper functions for model-specific tool handling
char* generate_model_tools_json(ModelRegistry* registry, const char* model_name, const ToolRegistry* tools);
int parse_model_tool_calls(ModelRegistry* registry, const char* model_name, 
                          const char* json_response, ToolCall** tool_calls, int* call_count);
char* format_model_tool_result_message(ModelRegistry* registry, const char* model_name, const ToolResult* result);
char* format_model_assistant_tool_message(ModelRegistry* registry, const char* model_name,
                                         const char* response_content, const ToolCall* tool_calls, int tool_call_count);

// Get the global model registry instance
ModelRegistry* get_model_registry(void);

#endif // MODEL_CAPABILITIES_H