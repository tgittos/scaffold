#include "model_capabilities.h"
#include "output_formatter.h"
#include "response_processing.h"
#include "tools_system.h"
#include <stdlib.h>
#include <string.h>

// Claude-specific tool message formatting - preserves raw JSON for tool_use blocks
static char* claude_format_assistant_tool_message(const char* response_content,
                                                 const ToolCall* tool_calls,
                                                 int tool_call_count) {
    (void)tool_calls;
    (void)tool_call_count;
    // For Claude/Anthropic, we save the raw JSON response to preserve tool_use blocks
    // This is because Anthropic requires exact tool_use/tool_result pairing
    if (response_content) {
        return strdup(response_content);
    }
    return NULL;
}

// Claude model capabilities
static ModelCapabilities claude_model = {
    .model_pattern = "claude",
    .supports_thinking_tags = 0,
    .thinking_start_tag = NULL,
    .thinking_end_tag = NULL,
    .process_response = process_simple_response,
    .supports_function_calling = 1,
    .generate_tools_json = generate_anthropic_tools_json,
    .parse_tool_calls = parse_anthropic_tool_calls,
    .format_tool_result_message = generate_single_tool_message,
    .format_assistant_tool_message = claude_format_assistant_tool_message,
    .supports_structured_output = 0,
    .supports_json_mode = 0,
    .max_context_length = 200000
};

int register_claude_models(ModelRegistry* registry) {
    return register_model_capabilities(registry, &claude_model);
}
