#include "conversation_state.h"
#include "../session/conversation_tracker.h"
#include "../llm/model_capabilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int conversation_append_assistant(AgentSession* session,
                                  const char* content,
                                  const ToolCall* calls,
                                  int count) {
    if (session == NULL) {
        return -1;
    }

    if (count > 0 && calls != NULL) {
        char* formatted = format_model_assistant_tool_message(
            session->model_registry, session->session_data.config.model,
            content, calls, count);

        if (formatted) {
            int rc = append_conversation_message(
                &session->session_data.conversation, "assistant", formatted);
            free(formatted);
            if (rc != 0) {
                fprintf(stderr, "Warning: Failed to save assistant response with tool calls to conversation history\n");
                return -1;
            }
            return 0;
        }

        char summary[256];
        snprintf(summary, sizeof(summary), "Used tools: ");
        for (int i = 0; i < count; i++) {
            if (i > 0) {
                strncat(summary, ", ", sizeof(summary) - strlen(summary) - 1);
            }
            strncat(summary, calls[i].name ? calls[i].name : "unknown",
                    sizeof(summary) - strlen(summary) - 1);
        }
        if (append_conversation_message(&session->session_data.conversation, "assistant", summary) != 0) {
            fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
            return -1;
        }
        return 0;
    }

    if (content != NULL) {
        if (append_conversation_message(&session->session_data.conversation, "assistant", content) != 0) {
            fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
            return -1;
        }
    }
    return 0;
}

int conversation_append_tool_results(AgentSession* session,
                                     const ToolResult* results,
                                     int result_count,
                                     const ToolCall* source_calls,
                                     const int* call_indices) {
    if (session == NULL || results == NULL || source_calls == NULL) {
        return -1;
    }

    for (int i = 0; i < result_count; i++) {
        int ci = call_indices ? call_indices[i] : i;

        if (append_tool_message(&session->session_data.conversation,
                               results[i].result, source_calls[ci].id,
                               source_calls[ci].name) != 0) {
            fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
        }
    }
    return 0;
}
