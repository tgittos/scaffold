#include "response_processing.h"
#include "../../ui/output_formatter.h"
#include <stdlib.h>
#include <string.h>

int process_thinking_response(const char* content, ParsedResponse* result) {
    if (!content || !result) {
        return -1;
    }

    result->thinking_content = NULL;
    result->response_content = NULL;

    const char* think_start = strstr(content, THINK_START_TAG);
    const char* think_end = strstr(content, THINK_END_TAG);

    if (think_start && think_end && think_end > think_start) {
        // Extract thinking content
        const char* thinking_content_start = think_start + THINK_START_TAG_LEN;
        size_t think_len = (size_t)(think_end - thinking_content_start);

        result->thinking_content = malloc(think_len + 1);
        if (!result->thinking_content) {
            return -1;
        }
        memcpy(result->thinking_content, thinking_content_start, think_len);
        result->thinking_content[think_len] = '\0';

        // Extract response content (everything after </think>)
        const char* response_start = think_end + THINK_END_TAG_LEN;

        // Skip leading whitespace
        while (*response_start && (*response_start == ' ' || *response_start == '\t' ||
               *response_start == '\n' || *response_start == '\r')) {
            response_start++;
        }

        if (*response_start) {
            size_t response_len = strlen(response_start);
            result->response_content = malloc(response_len + 1);
            if (!result->response_content) {
                free(result->thinking_content);
                result->thinking_content = NULL;
                return -1;
            }
            memcpy(result->response_content, response_start, response_len + 1);
        }
    } else {
        // No thinking tags, entire content is the response
        size_t content_len = strlen(content);
        result->response_content = malloc(content_len + 1);
        if (!result->response_content) {
            return -1;
        }
        memcpy(result->response_content, content, content_len + 1);
    }

    return 0;
}

int process_simple_response(const char* content, ParsedResponse* result) {
    if (!content || !result) {
        return -1;
    }

    result->thinking_content = NULL;
    result->response_content = NULL;

    size_t content_len = strlen(content);
    result->response_content = malloc(content_len + 1);
    if (!result->response_content) {
        return -1;
    }
    memcpy(result->response_content, content, content_len + 1);

    return 0;
}
