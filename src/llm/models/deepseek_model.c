#include "model_capabilities.h"
#include "ui/output_formatter.h"
#include "response_processing.h"
#include "tools_system.h"

// DeepSeek model capabilities
static ModelCapabilities deepseek_model = {
    .model_pattern = "deepseek",
    .supports_thinking_tags = 1,
    .thinking_start_tag = THINK_START_TAG,
    .thinking_end_tag = THINK_END_TAG,
    .process_response = process_thinking_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = NULL,
    .max_context_length = 128000
};

int register_deepseek_models(ModelRegistry* registry) {
    return register_model_capabilities(registry, &deepseek_model);
}
