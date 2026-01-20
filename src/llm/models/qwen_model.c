#include "model_capabilities.h"
#include "output_formatter.h"
#include "response_processing.h"
#include "tools_system.h"

// Qwen model capabilities
static ModelCapabilities qwen_model = {
    .model_pattern = "qwen",
    .supports_thinking_tags = 1,
    .thinking_start_tag = THINK_START_TAG,
    .thinking_end_tag = THINK_END_TAG,
    .process_response = process_thinking_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_tools_json,
    .parse_tool_calls = parse_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = NULL,
    .supports_structured_output = 0,
    .supports_json_mode = 0,
    .max_context_length = 32768
};

int register_qwen_models(ModelRegistry* registry) {
    return register_model_capabilities(registry, &qwen_model);
}
