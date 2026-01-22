#include "json_output.h"
#include "tools_system.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void json_output_init(void) {
    // Currently no initialization needed
    // Future: could set up buffering or other state
}

void json_output_assistant_text(const char* text, int input_tokens, int output_tokens) {
    if (text == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "assistant");

    // Build message object
    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        cJSON_Delete(root);
        return;
    }

    // Build content array with text block
    cJSON *content = cJSON_CreateArray();
    if (content == NULL) {
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    cJSON *text_block = cJSON_CreateObject();
    if (text_block != NULL) {
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", text);
        cJSON_AddItemToArray(content, text_block);
    }

    cJSON_AddItemToObject(message, "content", content);

    // Build usage object
    cJSON *usage = cJSON_CreateObject();
    if (usage != NULL) {
        cJSON_AddNumberToObject(usage, "input_tokens", input_tokens);
        cJSON_AddNumberToObject(usage, "output_tokens", output_tokens);
        cJSON_AddItemToObject(message, "usage", usage);
    }

    cJSON_AddItemToObject(root, "message", message);

    // Print JSON line
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        printf("%s\n", json_str);
        fflush(stdout);
        free(json_str);
    }

    cJSON_Delete(root);
}

void json_output_assistant_tool_calls(StreamingToolUse* tools, int count, int input_tokens, int output_tokens) {
    if (tools == NULL || count <= 0) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "assistant");

    // Build message object
    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        cJSON_Delete(root);
        return;
    }

    // Build content array with tool_use blocks
    cJSON *content = cJSON_CreateArray();
    if (content == NULL) {
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    for (int i = 0; i < count; i++) {
        cJSON *tool_use = cJSON_CreateObject();
        if (tool_use == NULL) {
            continue;
        }

        cJSON_AddStringToObject(tool_use, "type", "tool_use");
        cJSON_AddStringToObject(tool_use, "id", tools[i].id ? tools[i].id : "");
        cJSON_AddStringToObject(tool_use, "name", tools[i].name ? tools[i].name : "");

        // Parse arguments_json as JSON object
        if (tools[i].arguments_json != NULL) {
            cJSON *input = cJSON_Parse(tools[i].arguments_json);
            if (input != NULL) {
                cJSON_AddItemToObject(tool_use, "input", input);
            } else {
                // Fallback to empty object if parse fails
                cJSON_AddItemToObject(tool_use, "input", cJSON_CreateObject());
            }
        } else {
            cJSON_AddItemToObject(tool_use, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToArray(content, tool_use);
    }

    cJSON_AddItemToObject(message, "content", content);

    // Build usage object
    cJSON *usage = cJSON_CreateObject();
    if (usage != NULL) {
        cJSON_AddNumberToObject(usage, "input_tokens", input_tokens);
        cJSON_AddNumberToObject(usage, "output_tokens", output_tokens);
        cJSON_AddItemToObject(message, "usage", usage);
    }

    cJSON_AddItemToObject(root, "message", message);

    // Print JSON line
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        printf("%s\n", json_str);
        fflush(stdout);
        free(json_str);
    }

    cJSON_Delete(root);
}

void json_output_assistant_tool_calls_buffered(ToolCall* tool_calls, int count, int input_tokens, int output_tokens) {
    if (tool_calls == NULL || count <= 0) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "assistant");

    // Build message object
    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        cJSON_Delete(root);
        return;
    }

    // Build content array with tool_use blocks
    cJSON *content = cJSON_CreateArray();
    if (content == NULL) {
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    for (int i = 0; i < count; i++) {
        cJSON *tool_use = cJSON_CreateObject();
        if (tool_use == NULL) {
            continue;
        }

        cJSON_AddStringToObject(tool_use, "type", "tool_use");
        cJSON_AddStringToObject(tool_use, "id", tool_calls[i].id ? tool_calls[i].id : "");
        cJSON_AddStringToObject(tool_use, "name", tool_calls[i].name ? tool_calls[i].name : "");

        // Parse arguments as JSON object
        if (tool_calls[i].arguments != NULL) {
            cJSON *input = cJSON_Parse(tool_calls[i].arguments);
            if (input != NULL) {
                cJSON_AddItemToObject(tool_use, "input", input);
            } else {
                // Fallback to empty object if parse fails
                cJSON_AddItemToObject(tool_use, "input", cJSON_CreateObject());
            }
        } else {
            cJSON_AddItemToObject(tool_use, "input", cJSON_CreateObject());
        }

        cJSON_AddItemToArray(content, tool_use);
    }

    cJSON_AddItemToObject(message, "content", content);

    // Build usage object
    cJSON *usage = cJSON_CreateObject();
    if (usage != NULL) {
        cJSON_AddNumberToObject(usage, "input_tokens", input_tokens);
        cJSON_AddNumberToObject(usage, "output_tokens", output_tokens);
        cJSON_AddItemToObject(message, "usage", usage);
    }

    cJSON_AddItemToObject(root, "message", message);

    // Print JSON line
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        printf("%s\n", json_str);
        fflush(stdout);
        free(json_str);
    }

    cJSON_Delete(root);
}

void json_output_tool_result(const char* tool_use_id, const char* content, bool is_error) {
    if (tool_use_id == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "user");

    // Build message object
    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        cJSON_Delete(root);
        return;
    }

    // Build content array with tool_result block
    cJSON *content_array = cJSON_CreateArray();
    if (content_array == NULL) {
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    cJSON *tool_result = cJSON_CreateObject();
    if (tool_result != NULL) {
        cJSON_AddStringToObject(tool_result, "type", "tool_result");
        cJSON_AddStringToObject(tool_result, "tool_use_id", tool_use_id);
        cJSON_AddStringToObject(tool_result, "content", content ? content : "");
        cJSON_AddBoolToObject(tool_result, "is_error", is_error);
        cJSON_AddItemToArray(content_array, tool_result);
    }

    cJSON_AddItemToObject(message, "content", content_array);
    cJSON_AddItemToObject(root, "message", message);

    // Print JSON line
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        printf("%s\n", json_str);
        fflush(stdout);
        free(json_str);
    }

    cJSON_Delete(root);
}

void json_output_system(const char* subtype, const char* message) {
    if (message == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "system");
    if (subtype != NULL) {
        cJSON_AddStringToObject(root, "subtype", subtype);
    }
    cJSON_AddStringToObject(root, "message", message);

    // Print JSON line
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        printf("%s\n", json_str);
        fflush(stdout);
        free(json_str);
    }

    cJSON_Delete(root);
}

void json_output_error(const char* error) {
    json_output_system("error", error ? error : "Unknown error");
}

void json_output_result(const char* result) {
    if (result == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(root, "type", "result");
    cJSON_AddStringToObject(root, "result", result);

    // Print JSON line
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str != NULL) {
        printf("%s\n", json_str);
        fflush(stdout);
        free(json_str);
    }

    cJSON_Delete(root);
}
