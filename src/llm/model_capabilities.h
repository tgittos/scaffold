#ifndef MODEL_CAPABILITIES_H
#define MODEL_CAPABILITIES_H

#include <stddef.h>

#include "output_formatter.h"
#include "tools_system.h"
#include "utils/ptrarray.h"

typedef struct {
    const char* model_pattern;  /* substring match, e.g. "qwen", "deepseek", "claude" */

    int supports_thinking_tags;
    const char* thinking_start_tag;
    const char* thinking_end_tag;
    
    int (*process_response)(const char* content, ParsedResponse* result);

    int supports_function_calling;
    char* (*generate_tools_json)(const ToolRegistry* registry);
    int (*parse_tool_calls)(const char* json_response, ToolCall** tool_calls, int* call_count);
    char* (*format_tool_result_message)(const ToolResult* result);
    char* (*format_assistant_tool_message)(const char* response_content, const ToolCall* tool_calls, int tool_call_count);
    
    int max_context_length;
} ModelCapabilities;

PTRARRAY_DECLARE(ModelRegistry, ModelCapabilities)

int init_model_registry(ModelRegistry* registry);
int register_model_capabilities(ModelRegistry* registry, ModelCapabilities* model);
ModelCapabilities* detect_model_capabilities(ModelRegistry* registry, const char* model_name);
void cleanup_model_registry(ModelRegistry* registry);

int register_qwen_models(ModelRegistry* registry);
int register_deepseek_models(ModelRegistry* registry);
int register_claude_models(ModelRegistry* registry);
int register_gpt_models(ModelRegistry* registry);
int register_default_model(ModelRegistry* registry);

char* generate_model_tools_json(ModelRegistry* registry, const char* model_name, const ToolRegistry* tools);
int parse_model_tool_calls(ModelRegistry* registry, const char* model_name, 
                          const char* json_response, ToolCall** tool_calls, int* call_count);
char* format_model_tool_result_message(ModelRegistry* registry, const char* model_name, const ToolResult* result);
char* format_model_assistant_tool_message(ModelRegistry* registry, const char* model_name,
                                         const char* response_content, const ToolCall* tool_calls, int tool_call_count);

ModelRegistry* get_model_registry(void);

#endif // MODEL_CAPABILITIES_H