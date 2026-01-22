#include "json_output.h"
#include "tools_system.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// JSON Type Constants
// =============================================================================

#define JSON_TYPE_ASSISTANT "assistant"
#define JSON_TYPE_USER "user"
#define JSON_TYPE_SYSTEM "system"
#define JSON_TYPE_RESULT "result"
#define JSON_CONTENT_TEXT "text"
#define JSON_CONTENT_TOOL_USE "tool_use"
#define JSON_CONTENT_TOOL_RESULT "tool_result"

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * Log an allocation failure to stderr (doesn't interfere with JSON on stdout).
 */
static void log_alloc_failure(const char* context) {
    fprintf(stderr, "json_output: allocation failed in %s\n", context);
}

/**
 * Print a cJSON object as a single line to stdout and free it.
 * Returns 0 on success, -1 on failure.
 */
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

/**
 * Create a usage object with token counts.
 * Returns NULL on failure (caller should handle cleanup).
 */
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

/**
 * Callback type for extracting tool call fields from different struct types.
 */
typedef struct {
    const char* id;
    const char* name;
    const char* arguments;
} ToolCallFields;

/**
 * Build assistant tool calls JSON using a generic field extractor.
 * This eliminates code duplication between streaming and buffered versions.
 */
static int build_assistant_tool_calls_json(
    int count,
    ToolCallFields (*get_fields)(void* tools, int index),
    void* tools,
    int input_tokens,
    int output_tokens
) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        log_alloc_failure("build_assistant_tool_calls_json root");
        return -1;
    }

    if (cJSON_AddStringToObject(root, "type", JSON_TYPE_ASSISTANT) == NULL) {
        log_alloc_failure("build_assistant_tool_calls_json type");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *message = cJSON_CreateObject();
    if (message == NULL) {
        log_alloc_failure("build_assistant_tool_calls_json message");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *content = cJSON_CreateArray();
    if (content == NULL) {
        log_alloc_failure("build_assistant_tool_calls_json content");
        cJSON_Delete(message);
        cJSON_Delete(root);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        ToolCallFields fields = get_fields(tools, i);

        cJSON *tool_use = cJSON_CreateObject();
        if (tool_use == NULL) {
            log_alloc_failure("build_assistant_tool_calls_json tool_use");
            continue;  // Skip this tool but try to output others
        }

        // Require valid id and name - skip tool if missing
        if (fields.id == NULL || fields.name == NULL) {
            fprintf(stderr, "json_output: skipping tool call with NULL id or name\n");
            cJSON_Delete(tool_use);
            continue;
        }

        cJSON_AddStringToObject(tool_use, "type", JSON_CONTENT_TOOL_USE);
        cJSON_AddStringToObject(tool_use, "id", fields.id);
        cJSON_AddStringToObject(tool_use, "name", fields.name);

        // Parse arguments as JSON object
        if (fields.arguments != NULL) {
            cJSON *input = cJSON_Parse(fields.arguments);
            if (input != NULL) {
                cJSON_AddItemToObject(tool_use, "input", input);
            } else {
                // Fallback to empty object if parse fails
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
    }

    cJSON_AddItemToObject(message, "content", content);

    // Add usage object (optional - continue even if it fails)
    cJSON *usage = create_usage_object(input_tokens, output_tokens);
    if (usage != NULL) {
        cJSON_AddItemToObject(message, "usage", usage);
    }

    cJSON_AddItemToObject(root, "message", message);

    return print_and_free_json(root);
}

// Field extractors for the two tool call struct types
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

// =============================================================================
// Public API
// =============================================================================

void json_output_init(void) {
    // No initialization currently needed.
    // This function exists for future extensibility (e.g., buffering setup).
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

    // Add usage object (optional - continue even if it fails)
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
