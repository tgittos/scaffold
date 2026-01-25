#include "model_capabilities.h"
#include "output_formatter.h"
#include "response_processing.h"

// Default model capabilities
static ModelCapabilities default_model = {
    .model_pattern = "default",
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = process_simple_response,
    .supports_function_calling = 0,
    .generate_tools_json = NULL,
    .parse_tool_calls = NULL,
    .format_tool_result_message = NULL,
    .format_assistant_tool_message = NULL,
    .max_context_length = 4096
};

int register_default_model(ModelRegistry* registry) {
    return register_model_capabilities(registry, &default_model);
}
