#include "../model_capabilities.h"
#include "../../util/json_escape.h"
#include "../../tools/tools_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GPT_TOOL_MSG_BASE_SIZE 200
#define GPT_TOOL_CALL_OVERHEAD 100
#define JSON_ESCAPE_MULTIPLIER 6
#define GPT_CONTENT_PADDING 50

char* gpt_format_assistant_tool_message(const char* response_content,
                                        const ToolCall* tool_calls,
                                        int tool_call_count) {
    if (tool_call_count > 0 && tool_calls != NULL) {
        size_t base_size = GPT_TOOL_MSG_BASE_SIZE;
        size_t content_size = response_content ?
            strlen(response_content) * JSON_ESCAPE_MULTIPLIER + GPT_CONTENT_PADDING : GPT_CONTENT_PADDING;

        for (int i = 0; i < tool_call_count; i++) {
            base_size += GPT_TOOL_CALL_OVERHEAD;
            base_size += tool_calls[i].id ? strlen(tool_calls[i].id) * JSON_ESCAPE_MULTIPLIER : 0;
            base_size += tool_calls[i].name ? strlen(tool_calls[i].name) * JSON_ESCAPE_MULTIPLIER : 0;
            base_size += tool_calls[i].arguments ? strlen(tool_calls[i].arguments) * JSON_ESCAPE_MULTIPLIER : 0;
        }

        size_t total_size = base_size + content_size;
        char* message = malloc(total_size);
        if (!message) {
            return NULL;
        }

        char* current = message;
        size_t remaining = total_size;

        int written = snprintf(current, remaining, "{\"role\": \"assistant\", ");
        if (written < 0 || written >= (int)remaining) {
            free(message);
            return NULL;
        }
        current += written;
        remaining -= written;

        if (response_content && strlen(response_content) > 0) {
            char* escaped_content = json_escape_string(response_content);
            if (!escaped_content) {
                free(message);
                return NULL;
            }

            written = snprintf(current, remaining, "\"content\": \"%s\", ", escaped_content);
            free(escaped_content);

            if (written < 0 || written >= (int)remaining) {
                free(message);
                return NULL;
            }
            current += written;
            remaining -= written;
        } else {
            written = snprintf(current, remaining, "\"content\": null, ");
            if (written < 0 || written >= (int)remaining) {
                free(message);
                return NULL;
            }
            current += written;
            remaining -= written;
        }

        written = snprintf(current, remaining, "\"tool_calls\": [");
        if (written < 0 || written >= (int)remaining) {
            free(message);
            return NULL;
        }
        current += written;
        remaining -= written;

        for (int i = 0; i < tool_call_count; i++) {
            const char* id = tool_calls[i].id ? tool_calls[i].id : "";
            const char* name = tool_calls[i].name ? tool_calls[i].name : "";
            const char* args = tool_calls[i].arguments ? tool_calls[i].arguments : "";

            char* escaped_args = json_escape_string(args);
            if (!escaped_args) {
                free(message);
                return NULL;
            }

            written = snprintf(current, remaining,
                             "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                             i > 0 ? ", " : "",
                             id,
                             name,
                             escaped_args);

            free(escaped_args);

            if (written < 0 || written >= (int)remaining) {
                free(message);
                return NULL;
            }
            current += written;
            remaining -= written;
        }

        written = snprintf(current, remaining, "]}");
        if (written < 0 || written >= (int)remaining) {
            free(message);
            return NULL;
        }

        return message;
    } else if (response_content) {
        char* escaped_content = json_escape_string(response_content);
        if (!escaped_content) {
            return NULL;
        }

        size_t msg_size = strlen(escaped_content) + GPT_CONTENT_PADDING;
        char* message = malloc(msg_size);
        if (!message) {
            free(escaped_content);
            return NULL;
        }

        snprintf(message, msg_size, "{\"content\": \"%s\"}", escaped_content);
        free(escaped_content);

        return message;
    }

    return NULL;
}
