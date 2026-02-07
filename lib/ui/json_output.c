#include "json_output.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_TYPE_ASSISTANT "assistant"
#define JSON_TYPE_USER "user"
#define JSON_TYPE_SYSTEM "system"
#define JSON_TYPE_RESULT "result"
#define JSON_CONTENT_TEXT "text"
#define JSON_CONTENT_TOOL_USE "tool_use"
#define JSON_CONTENT_TOOL_RESULT "tool_result"

static void log_alloc_failure(const char* context) {
    fprintf(stderr, "json_output: allocation failed in %s\n", context);
}

static int print_and_free_json(cJSON* root) {
    if (root == NULL) {
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        log_alloc_failure("cJSON_PrintUnformatted");
        cJSON_Delete(root);
        return -1;
    }

    printf("%s\n", json_str);
    fflush(stdout);
    free(json_str);
    cJSON_Delete(root);
    return 0;
}

static cJSON* create_usage_object(int input_tokens, int output_tokens) {
    cJSON *usage = cJSON_CreateObject();
    if (usage == NULL) {
        log_alloc_failure("create_usage_object");
        return NULL;
    }

    if (cJSON_AddNumberToObject(usage, "input_tokens", input_tokens) == NULL ||
        cJSON_AddNumberToObject(usage, "output_tokens", output_tokens) == NULL) {
        log_alloc_failure("create_usage_object fields");
        cJSON_Delete(usage);
        return NULL;
    }

    return usage;
}

typedef struct {
    const char* id;
    const char* name;
    const char* arguments;
} ToolCallFields;

static int emit_single_tool_call_json(ToolCallFields fields, bool include_usage, int input_tokens, int output_tokens) {
    if (fields.id == NULL || fields.name == NULL) {
        fprintf(stderr, "json_output: skipping tool call with NULL id or name\n");
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_alloc_failure("emit_single_tool_call_json root");
        return -1;
    }

    cJSON_AddStringToObject(root, "type", JSON_TYPE_ASSISTANT);

    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        log_alloc_failure("emit_single_tool_call_json message");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *content = cJSON_CreateArray();
    if (content == NULL) {
        log_alloc_failure("emit_single_tool_call_json content");
        cJSON_Delete(message);
        cJSON_Delete(root);
        return -1;
    }

    cJSON *tool_use = cJSON_CreateObject();
    if (tool_use == NULL) {
        log_alloc_failure("emit_single_tool_call_json tool_use");
        cJSON_Delete(content);
        cJSON_Delete(message);
        cJSON_Delete(root);
        return -1;
    }

    cJSON_AddStringToObject(tool_use, "type", JSON_CONTENT_TOOL_USE);
    cJSON_AddStringToObject(tool_use, "id", fields.id);
    cJSON_AddStringToObject(tool_use, "name", fields.name);

    if (fields.arguments != NULL) {
        cJSON *input = cJSON_Parse(fields.arguments);
        if (input != NULL) {
            cJSON_AddItemToObject(tool_use, "input", input);
        } else {
            cJSON *empty = cJSON_CreateObject();
            if (empty != NULL) {
                cJSON_AddItemToObject(tool_use, "input", empty);
            }
        }
    } else {
        cJSON *empty = cJSON_CreateObject();
        if (empty != NULL) {
            cJSON_AddItemToObject(tool_use, "input", empty);
        }
    }

    cJSON_AddItemToArray(content, tool_use);
    cJSON_AddItemToObject(message, "content", content);

    if (include_usage) {
        cJSON *usage = create_usage_object(input_tokens, output_tokens);
        if (usage != NULL) {
            cJSON_AddItemToObject(message, "usage", usage);
        }
    }

    cJSON_AddItemToObject(root, "message", message);

    return print_and_free_json(root);
}

static int build_assistant_tool_calls_json(
    int count,
    ToolCallFields (*get_fields)(void* tools, int index),
    void* tools,
    int input_tokens,
    int output_tokens
) {
    int result = 0;
    for (int i = 0; i < count; i++) {
        ToolCallFields fields = get_fields(tools, i);
        bool last = (i == count - 1);
        if (emit_single_tool_call_json(fields, last, input_tokens, output_tokens) != 0) {
            result = -1;
        }
    }
    return result;
}

static ToolCallFields get_streaming_tool_fields(void* tools, int index) {
    StreamingToolUse* arr = (StreamingToolUse*)tools;
    return (ToolCallFields){
        .id = arr[index].id,
        .name = arr[index].name,
        .arguments = arr[index].arguments_json
    };
}

static ToolCallFields get_buffered_tool_fields(void* tools, int index) {
    ToolCall* arr = (ToolCall*)tools;
    return (ToolCallFields){
        .id = arr[index].id,
        .name = arr[index].name,
        .arguments = arr[index].arguments
    };
}

void json_output_init(void) {
    /* Intentional no-op: provides a consistent init/cleanup lifecycle pattern. */
}

void json_output_assistant_text(const char* text, int input_tokens, int output_tokens) {
    if (text == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_alloc_failure("json_output_assistant_text root");
        return;
    }

    if (cJSON_AddStringToObject(root, "type", JSON_TYPE_ASSISTANT) == NULL) {
        log_alloc_failure("json_output_assistant_text type");
        cJSON_Delete(root);
        return;
    }

    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        log_alloc_failure("json_output_assistant_text message");
        cJSON_Delete(root);
        return;
    }

    cJSON *content = cJSON_CreateArray();
    if (content == NULL) {
        log_alloc_failure("json_output_assistant_text content");
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    cJSON *text_block = cJSON_CreateObject();
    if (text_block == NULL) {
        log_alloc_failure("json_output_assistant_text text_block");
        cJSON_Delete(content);
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    cJSON_AddStringToObject(text_block, "type", JSON_CONTENT_TEXT);
    cJSON_AddStringToObject(text_block, "text", text);
    cJSON_AddItemToArray(content, text_block);
    cJSON_AddItemToObject(message, "content", content);

    cJSON *usage = create_usage_object(input_tokens, output_tokens);
    if (usage != NULL) {
        cJSON_AddItemToObject(message, "usage", usage);
    }

    cJSON_AddItemToObject(root, "message", message);

    print_and_free_json(root);
}

void json_output_assistant_tool_calls(StreamingToolUse* tools, int count, int input_tokens, int output_tokens) {
    if (tools == NULL || count <= 0) {
        return;
    }

    build_assistant_tool_calls_json(count, get_streaming_tool_fields, tools, input_tokens, output_tokens);
}

void json_output_assistant_tool_calls_buffered(ToolCall* tool_calls, int count, int input_tokens, int output_tokens) {
    if (tool_calls == NULL || count <= 0) {
        return;
    }

    build_assistant_tool_calls_json(count, get_buffered_tool_fields, tool_calls, input_tokens, output_tokens);
}

void json_output_tool_result(const char* tool_use_id, const char* content, bool is_error) {
    if (tool_use_id == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_alloc_failure("json_output_tool_result root");
        return;
    }

    if (cJSON_AddStringToObject(root, "type", JSON_TYPE_USER) == NULL) {
        log_alloc_failure("json_output_tool_result type");
        cJSON_Delete(root);
        return;
    }

    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        log_alloc_failure("json_output_tool_result message");
        cJSON_Delete(root);
        return;
    }

    cJSON *content_array = cJSON_CreateArray();
    if (content_array == NULL) {
        log_alloc_failure("json_output_tool_result content_array");
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    cJSON *tool_result = cJSON_CreateObject();
    if (tool_result == NULL) {
        log_alloc_failure("json_output_tool_result tool_result");
        cJSON_Delete(content_array);
        cJSON_Delete(message);
        cJSON_Delete(root);
        return;
    }

    cJSON_AddStringToObject(tool_result, "type", JSON_CONTENT_TOOL_RESULT);
    cJSON_AddStringToObject(tool_result, "tool_use_id", tool_use_id);
    cJSON_AddStringToObject(tool_result, "content", content ? content : "");
    cJSON_AddBoolToObject(tool_result, "is_error", is_error);
    cJSON_AddItemToArray(content_array, tool_result);

    cJSON_AddItemToObject(message, "content", content_array);
    cJSON_AddItemToObject(root, "message", message);

    print_and_free_json(root);
}

void json_output_system(const char* subtype, const char* message) {
    if (message == NULL) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_alloc_failure("json_output_system root");
        return;
    }

    cJSON_AddStringToObject(root, "type", JSON_TYPE_SYSTEM);
    if (subtype != NULL) {
        cJSON_AddStringToObject(root, "subtype", subtype);
    }
    cJSON_AddStringToObject(root, "message", message);

    print_and_free_json(root);
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
        log_alloc_failure("json_output_result root");
        return;
    }

    cJSON_AddStringToObject(root, "type", JSON_TYPE_RESULT);
    cJSON_AddStringToObject(root, "result", result);

    print_and_free_json(root);
}
