#include "../model_capabilities.h"
#include "../../tools/tools_system.h"
#include "response_processing.h"
#include <stdlib.h>
#include <string.h>

/* Provider-specific format functions (defined in gpt_model.c / claude_model.c) */
char* gpt_format_assistant_tool_message(const char* response_content,
                                        const ToolCall* tool_calls,
                                        int tool_call_count);
char* claude_format_assistant_tool_message(const char* response_content,
                                           const ToolCall* tool_calls,
                                           int tool_call_count);

typedef struct {
    const char *pattern;
    const char *provider;     /* "openai", "anthropic", "none" */
    int max_context;
    int thinking;
    const char *think_start;
    const char *think_end;
} ModelDataEntry;

static const ModelDataEntry MODEL_DATA[] = {
    { "gpt",      "openai",    128000, 0, NULL,      NULL },
    { "o1",       "openai",    128000, 0, NULL,      NULL },
    { "o4",       "openai",    128000, 0, NULL,      NULL },
    { "qwen",     "openai",     32768, 1, "<think>", "</think>" },
    { "deepseek", "openai",    128000, 1, "<think>", "</think>" },
    { "claude",   "anthropic", 200000, 0, NULL,      NULL },
    { "default",  "none",        4096, 0, NULL,      NULL },
};
#define MODEL_DATA_COUNT (sizeof(MODEL_DATA) / sizeof(MODEL_DATA[0]))

int register_all_models(ModelRegistry* registry) {
    if (!registry) return -1;

    for (size_t i = 0; i < MODEL_DATA_COUNT; i++) {
        const ModelDataEntry *e = &MODEL_DATA[i];

        ModelCapabilities *m = calloc(1, sizeof(ModelCapabilities));
        if (!m) return -1;

        m->model_pattern      = e->pattern;
        m->max_context_length  = e->max_context;
        m->supports_thinking_tags = e->thinking;
        m->thinking_start_tag  = e->think_start;
        m->thinking_end_tag    = e->think_end;

        m->process_response = e->thinking
            ? process_thinking_response
            : process_simple_response;

        if (strcmp(e->provider, "openai") == 0) {
            m->supports_function_calling    = 1;
            m->generate_tools_json          = generate_tools_json;
            m->parse_tool_calls             = parse_tool_calls;
            m->format_tool_result_message   = generate_single_tool_message;
            m->format_assistant_tool_message = gpt_format_assistant_tool_message;
        } else if (strcmp(e->provider, "anthropic") == 0) {
            m->supports_function_calling    = 1;
            m->generate_tools_json          = generate_anthropic_tools_json;
            m->parse_tool_calls             = parse_anthropic_tool_calls;
            m->format_tool_result_message   = generate_single_tool_message;
            m->format_assistant_tool_message = claude_format_assistant_tool_message;
        }
        /* "none" provider: all function pointers stay NULL from calloc */

        if (register_model_capabilities(registry, m) != 0) {
            free(m);
            return -1;
        }
    }

    return 0;
}
