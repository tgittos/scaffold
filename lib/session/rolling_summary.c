#include "rolling_summary.h"
#include "../llm/llm_provider.h"
#include "../../src/network/http_client.h"
#include "../util/debug_output.h"
#include "token_manager.h"
#include "../../src/network/streaming.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>

#define SUMMARY_INITIAL_BUFFER_SIZE 8192
#define SUMMARY_MAX_RESPONSE_TOKENS 500

void rolling_summary_init(RollingSummary* summary) {
    if (summary == NULL) return;

    summary->summary_text = NULL;
    summary->estimated_tokens = 0;
    summary->messages_summarized = 0;
}

void rolling_summary_cleanup(RollingSummary* summary) {
    if (summary == NULL) return;

    free(summary->summary_text);
    summary->summary_text = NULL;
    summary->estimated_tokens = 0;
    summary->messages_summarized = 0;
}

static char* format_messages_for_summary(const ConversationMessage* messages, int message_count) {
    if (messages == NULL || message_count <= 0) {
        return NULL;
    }

    size_t buffer_size = SUMMARY_INITIAL_BUFFER_SIZE;
    for (int i = 0; i < message_count; i++) {
        if (messages[i].content != NULL) {
            buffer_size += strlen(messages[i].content) + 64;
        }
    }

    char* buffer = malloc(buffer_size);
    if (buffer == NULL) {
        return NULL;
    }
    buffer[0] = '\0';

    size_t offset = 0;
    for (int i = 0; i < message_count; i++) {
        const ConversationMessage* msg = &messages[i];

        if (msg->role != NULL && strcmp(msg->role, "tool") == 0) {
            continue;
        }

        const char* role = msg->role ? msg->role : "unknown";
        const char* content = msg->content ? msg->content : "";

        const size_t max_content_length = 800;
        char truncated_content[max_content_length + 4];  // +4 for "..." and null
        if (strlen(content) > max_content_length) {
            strncpy(truncated_content, content, max_content_length - 3);
            truncated_content[max_content_length - 3] = '\0';
            strcat(truncated_content, "...");
            content = truncated_content;
        }

        int written = snprintf(buffer + offset, buffer_size - offset,
                              "**%s**: %s\n\n", role, content);
        if (written < 0 || (size_t)written >= buffer_size - offset) {
            break;
        }
        offset += written;
    }

    return buffer;
}

static char* build_summary_prompt(const char* formatted_messages, const char* existing_summary) {
    const char* prompt_template =
        "Summarize this conversation segment being compacted for context management.\n\n"
        "MESSAGES:\n%s\n"
        "EXISTING SUMMARY (merge with above if present):\n%s\n\n"
        "Provide a concise summary (under 500 tokens) capturing:\n"
        "- Key decisions made\n"
        "- Constraints or requirements established\n"
        "- User preferences expressed\n"
        "- Technical context needed to continue\n\n"
        "Focus on information needed to continue the conversation effectively.";

    const char* existing = existing_summary ? existing_summary : "None";

    size_t prompt_size = strlen(prompt_template) + strlen(formatted_messages) + strlen(existing) + 1;
    char* prompt = malloc(prompt_size);
    if (prompt == NULL) {
        return NULL;
    }

    snprintf(prompt, prompt_size, prompt_template, formatted_messages, existing);
    return prompt;
}

static char* extract_content_from_response(const char* response_data, int api_type) {
    if (response_data == NULL) {
        return NULL;
    }

    cJSON* root = cJSON_Parse(response_data);
    if (root == NULL) {
        debug_printf("Failed to parse summary response JSON\n");
        return NULL;
    }

    char* content = NULL;

    if (api_type == 1) {
        cJSON* content_array = cJSON_GetObjectItem(root, "content");
        if (cJSON_IsArray(content_array) && cJSON_GetArraySize(content_array) > 0) {
            cJSON* first_block = cJSON_GetArrayItem(content_array, 0);
            cJSON* text = cJSON_GetObjectItem(first_block, "text");
            if (cJSON_IsString(text) && text->valuestring != NULL) {
                content = strdup(text->valuestring);
            }
        }
    } else {
        cJSON* choices = cJSON_GetObjectItem(root, "choices");
        if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
            cJSON* first_choice = cJSON_GetArrayItem(choices, 0);
            cJSON* message = cJSON_GetObjectItem(first_choice, "message");
            if (message != NULL) {
                cJSON* text = cJSON_GetObjectItem(message, "content");
                if (cJSON_IsString(text) && text->valuestring != NULL) {
                    content = strdup(text->valuestring);
                }
            }
        }
    }

    cJSON_Delete(root);
    return content;
}

static char* build_api_request_json(int api_type, const char* model, const char* prompt) {
    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddNumberToObject(root, "max_tokens", SUMMARY_MAX_RESPONSE_TOKENS);

    if (api_type == 1) {
        cJSON* messages = cJSON_CreateArray();
        cJSON* user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", prompt);
        cJSON_AddItemToArray(messages, user_msg);
        cJSON_AddItemToObject(root, "messages", messages);
    } else {
        cJSON* messages = cJSON_CreateArray();
        cJSON* user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", prompt);
        cJSON_AddItemToArray(messages, user_msg);
        cJSON_AddItemToObject(root, "messages", messages);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

int generate_rolling_summary(
    const char* api_url,
    const char* api_key,
    int api_type,
    const char* model,
    const ConversationMessage* messages,
    int message_count,
    const char* existing_summary,
    char** out_summary
) {
    if (api_url == NULL || model == NULL || messages == NULL ||
        message_count <= 0 || out_summary == NULL) {
        return -1;
    }

    *out_summary = NULL;

    // Skip summary generation for local AI without authentication
    // (they may not support the chat completions format needed for summarization)
    if (api_key == NULL && api_type == 2) {
        debug_printf("Skipping rolling summary: local AI without authentication\n");
        return -1;
    }

    char* formatted_messages = format_messages_for_summary(messages, message_count);
    if (formatted_messages == NULL) {
        debug_printf("Failed to format messages for summary\n");
        return -1;
    }

    char* prompt = build_summary_prompt(formatted_messages, existing_summary);
    free(formatted_messages);
    if (prompt == NULL) {
        debug_printf("Failed to build summary prompt\n");
        return -1;
    }

    debug_printf("Generating rolling summary for %d messages\n", message_count);
    debug_printf("Summary prompt: %.200s...\n", prompt);

    char* post_data = build_api_request_json(api_type, model, prompt);
    free(prompt);
    if (post_data == NULL) {
        debug_printf("Failed to build API request JSON\n");
        return -1;
    }

    debug_printf("Summary API request: %s\n", post_data);

    char auth_header[512] = {0};
    char anthropic_version[128] = "anthropic-version: 2023-06-01";
    char content_type[64] = "Content-Type: application/json";
    const char* headers[4] = {NULL, NULL, NULL, NULL};
    int header_count = 0;

    if (api_key != NULL) {
        if (api_type == 1) {
            int ret = snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
            headers[header_count++] = anthropic_version;
            headers[header_count++] = content_type;
        } else {
            int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
        }
    }

    struct HTTPResponse response = {0};
    int result = http_post_with_headers(api_url, post_data, headers, &response);
    free(post_data);

    if (result != 0) {
        debug_printf("Summary API request failed with code %d\n", result);
        cleanup_response(&response);
        return -1;
    }

    if (response.data == NULL || response.size == 0) {
        debug_printf("Empty response from summary API\n");
        cleanup_response(&response);
        return -1;
    }

    debug_printf("Summary API response: %.500s...\n", response.data);

    char* summary_content = extract_content_from_response(response.data, api_type);
    cleanup_response(&response);

    if (summary_content == NULL) {
        debug_printf("Failed to extract summary content from response\n");
        return -1;
    }

    debug_printf("Generated summary: %.200s...\n", summary_content);

    *out_summary = summary_content;
    return 0;
}
