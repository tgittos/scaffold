#ifndef MODEL_CAPABILITIES_H
#define MODEL_CAPABILITIES_H

#include <stddef.h>

#include "../types.h"
#include "../util/ptrarray.h"

/* Forward declarations to break cross-layer header dependencies */
typedef struct ParsedResponse ParsedResponse;

typedef struct {
    const char* model_pattern;  /* substring match, e.g. "qwen", "deepseek", "claude" */

    int supports_thinking_tags;
    const char* thinking_start_tag;
    const char* thinking_end_tag;

    int (*process_response)(const char* content, ParsedResponse* result);

    int max_context_length;
} ModelCapabilities;

PTRARRAY_DECLARE(ModelRegistry, ModelCapabilities)

int init_model_registry(ModelRegistry* registry);
int register_model_capabilities(ModelRegistry* registry, ModelCapabilities* model);
ModelCapabilities* detect_model_capabilities(ModelRegistry* registry, const char* model_name);
void cleanup_model_registry(ModelRegistry* registry);

int register_all_models(ModelRegistry* registry);

char* gpt_format_assistant_tool_message(const char* response_content,
                                        const ToolCall* tool_calls,
                                        int tool_call_count);
char* claude_format_assistant_tool_message(const char* response_content,
                                           const ToolCall* tool_calls,
                                           int tool_call_count);

ModelRegistry* get_model_registry(void);
void cleanup_model_registry_singleton(void);

#endif // MODEL_CAPABILITIES_H
