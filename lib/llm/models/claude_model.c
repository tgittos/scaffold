#include "../model_capabilities.h"
#include "../../tools/tools_system.h"
#include <stdlib.h>
#include <string.h>

char* claude_format_assistant_tool_message(const char* response_content,
                                           const ToolCall* tool_calls,
                                           int tool_call_count) {
    (void)tool_calls;
    (void)tool_call_count;
    if (response_content) {
        return strdup(response_content);
    }
    return NULL;
}
