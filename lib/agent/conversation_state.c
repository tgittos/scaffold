#include "conversation_state.h"
#include "../session/conversation_tracker.h"
#include "../llm/model_capabilities.h"
#include "../util/json_escape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* conversation_build_assistant_tool_message(const char* content,
                                                const ToolCall* tool_calls,
                                                int call_count) {
    if (call_count <= 0 || tool_calls == NULL) {
        return content ? strdup(content) : NULL;
    }

    size_t base_size = 200;
    size_t content_len = content ? strlen(content) : 0;

    if (content_len > SIZE_MAX / 2 - 50) {
        return NULL;
    }
    size_t content_size = content_len * 2 + 50;

    if (call_count > 0 && (size_t)call_count > SIZE_MAX / 200) {
        return NULL;
    }
    size_t tools_size = (size_t)call_count * 200;

    if (base_size > SIZE_MAX - content_size ||
        base_size + content_size > SIZE_MAX - tools_size) {
        return NULL;
    }

    char* message = malloc(base_size + content_size + tools_size);
    if (message == NULL) {
        return NULL;
    }

    char* escaped_content = json_escape_string(content ? content : "");
    if (escaped_content == NULL) {
        free(message);
        return NULL;
    }

    int written = snprintf(message, base_size + content_size + tools_size,
                          "{\"role\": \"assistant\", \"content\": \"%s\", \"tool_calls\": [",
                          escaped_content);
    free(escaped_content);

    if (written < 0) {
        free(message);
        return NULL;
    }

    for (int i = 0; i < call_count; i++) {
        char* escaped_args = json_escape_string(tool_calls[i].arguments ? tool_calls[i].arguments : "{}");
        if (escaped_args == NULL) {
            free(message);
            return NULL;
        }

        const char* id = tool_calls[i].id ? tool_calls[i].id : "";
        const char* name = tool_calls[i].name ? tool_calls[i].name : "";
        size_t tool_json_size = strlen(id) + strlen(name) + strlen(escaped_args) + 100;
        char* tool_call_json = malloc(tool_json_size);
        if (tool_call_json == NULL) {
            free(escaped_args);
            free(message);
            return NULL;
        }

        int tool_written = snprintf(tool_call_json, tool_json_size,
                                   "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                                   i > 0 ? ", " : "",
                                   id, name, escaped_args);
        free(escaped_args);

        if (tool_written < 0 || tool_written >= (int)tool_json_size) {
            free(tool_call_json);
            free(message);
            return NULL;
        }

        size_t current_len = strlen(message);
        size_t needed_len = current_len + strlen(tool_call_json) + 3;
        if (needed_len > base_size + content_size + tools_size) {
            char* new_message = realloc(message, needed_len + 100);
            if (new_message == NULL) {
                free(tool_call_json);
                free(message);
                return NULL;
            }
            message = new_message;
        }

        strcat(message, tool_call_json);
        free(tool_call_json);
    }

    strcat(message, "]}");
    return message;
}

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
