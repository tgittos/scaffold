#include "output_formatter.h"
#include "debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static char *extract_json_string(const char *json, const char *key) {
    if (!json || !key) {
        return NULL;
    }
    
    // Build search pattern: "key":"
    char pattern[256];
    memset(pattern, 0, sizeof(pattern));  // Explicitly zero-initialize for Valgrind
    int ret = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (ret < 0 || ret >= (int)sizeof(pattern)) {
        return NULL;
    }
    
    const char *key_pos = strstr(json, pattern);
    if (!key_pos) {
        return NULL;
    }
    
    const char *start = key_pos + strlen(pattern);
    
    // Skip whitespace
    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }
    
    // Must start with quote
    if (*start != '"') {
        return NULL;
    }
    start++; // Skip opening quote
    
    const char *end = start;
    
    // Find the end of the string value, handling escaped quotes and newlines
    while (*end) {
        if (*end == '"') {
            break; // Found closing quote
        } else if (*end == '\\' && *(end + 1)) {
            end += 2; // Skip escaped character (including \n, \", \\, etc.)
        } else {
            end++;
        }
    }
    
    if (*end != '"') {
        return NULL;
    }
    
    if (end < start) {
        return NULL;  // Invalid range
    }
    
    size_t len = (size_t)(end - start);
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }
    
    if (len > 0) {
        memcpy(result, start, len);
    }
    result[len] = '\0';
    
    return result;
}

static void unescape_json_string(char *str) {
    if (!str) return;
    
    char *src = str;
    char *dst = str;
    
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            switch (*(src + 1)) {
                case 'n':
                    *dst++ = '\n';
                    src += 2;
                    break;
                case 't':
                    *dst++ = '\t';
                    src += 2;
                    break;
                case 'r':
                    *dst++ = '\r';
                    src += 2;
                    break;
                case '\\':
                    *dst++ = '\\';
                    src += 2;
                    break;
                case '"':
                    *dst++ = '"';
                    src += 2;
                    break;
                default:
                    // Unknown escape, keep as-is
                    *dst++ = *src++;
                    break;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static int extract_json_int(const char *json, const char *key) {
    if (!json || !key) {
        return -1;
    }
    
    // Build search pattern: "key":
    char pattern[256];
    memset(pattern, 0, sizeof(pattern));  // Explicitly zero-initialize for Valgrind
    int ret = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (ret < 0 || ret >= (int)sizeof(pattern)) {
        return -1;
    }
    
    const char *start = strstr(json, pattern);
    if (!start) {
        return -1;
    }
    
    start += strlen(pattern);
    
    // Skip whitespace
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    
    int value = 0;
    if (sscanf(start, "%d", &value) != 1) {
        return -1;
    }
    
    return value;
}

static void separate_thinking_and_response(const char *content, char **thinking, char **response) {
    *thinking = NULL;
    *response = NULL;
    
    if (!content) {
        return;
    }
    
    // Look for <think> and </think> tags
    const char *think_start = strstr(content, "<think>");
    const char *think_end = strstr(content, "</think>");
    
    if (think_start && think_end && think_end > think_start) {
        // Extract thinking content
        think_start += 7; // Skip "<think>"
        size_t think_len = (size_t)(think_end - think_start);
        *thinking = malloc(think_len + 1);
        if (*thinking) {
            memcpy(*thinking, think_start, think_len);
            (*thinking)[think_len] = '\0';
        }
        
        // Extract response content (everything after </think>)
        const char *response_start = think_end + 8; // Skip "</think>"
        
        // Skip leading whitespace
        while (*response_start && (*response_start == ' ' || *response_start == '\t' || 
               *response_start == '\n' || *response_start == '\r')) {
            response_start++;
        }
        
        if (*response_start) {
            size_t response_len = strlen(response_start);
            *response = malloc(response_len + 1);
            if (*response) {
                strcpy(*response, response_start);
            }
        }
    } else {
        // No thinking tags, entire content is the response
        size_t content_len = strlen(content);
        *response = malloc(content_len + 1);
        if (*response) {
            strcpy(*response, content);
        }
    }
}

int parse_api_response(const char *json_response, ParsedResponse *result) {
    if (!json_response || !result) {
        return -1;
    }
    
    // Initialize result
    result->thinking_content = NULL;
    result->response_content = NULL;
    result->prompt_tokens = -1;
    result->completion_tokens = -1;
    result->total_tokens = -1;
    
    // Extract content from choices[0].message.content
    // Look for "content": pattern in the message object
    const char *message_start = strstr(json_response, "\"message\":");
    if (!message_start) {
        return -1;
    }
    
    // Check if content field exists
    const char *content_pos = strstr(message_start, "\"content\":");
    if (!content_pos) {
        // No content field - this is valid for tool calls
        // Check if there are tool_calls instead
        const char *tool_calls_pos = strstr(message_start, "\"tool_calls\":");
        if (tool_calls_pos) {
            // This is a tool call response with no content - valid, leave fields as NULL
            // But still extract token usage before returning
            const char *usage_start = strstr(json_response, "\"usage\":");
            if (usage_start) {
                result->prompt_tokens = extract_json_int(usage_start, "prompt_tokens");
                result->completion_tokens = extract_json_int(usage_start, "completion_tokens");
                result->total_tokens = extract_json_int(usage_start, "total_tokens");
            }
            return 0;
        } else {
            // No content and no tool_calls - invalid
            return -1;
        }
    }
    
    const char *value_start = content_pos + strlen("\"content\":");
    // Skip whitespace
    while (*value_start && (*value_start == ' ' || *value_start == '\t' || *value_start == '\n' || *value_start == '\r')) {
        value_start++;
    }
    
    // Check if content is null (common for tool calls)
    if (strncmp(value_start, "null", 4) == 0) {
        // Content is null - this is valid for tool calls, leave result fields as NULL
    } else if (*value_start == '"') {
        // Content is a string - extract it normally
        char *raw_content = extract_json_string(message_start, "content");
        if (!raw_content) {
            return -1; // Failed to extract content string
        }
        
        // Unescape JSON strings (convert \n to actual newlines, etc.)
        unescape_json_string(raw_content);
        
        // Separate thinking from response
        separate_thinking_and_response(raw_content, &result->thinking_content, &result->response_content);
        free(raw_content);
    } else {
        // Content is neither null nor a string - invalid format
        return -1;
    }
    
    // Extract token usage
    const char *usage_start = strstr(json_response, "\"usage\":");
    if (usage_start) {
        result->prompt_tokens = extract_json_int(usage_start, "prompt_tokens");
        result->completion_tokens = extract_json_int(usage_start, "completion_tokens");
        result->total_tokens = extract_json_int(usage_start, "total_tokens");
    }
    
    return 0;
}

void print_formatted_response(const ParsedResponse *response) {
    if (!response) {
        return;
    }
    
    // Print thinking content in dim gray if present
    if (response->thinking_content) {
        printf(ANSI_DIM ANSI_GRAY "%s" ANSI_RESET "\n\n", response->thinking_content);
    }
    
    // Print the main response content prominently (normal text)
    if (response->response_content) {
        printf("%s\n", response->response_content);
    }
    
    // Print token usage in gray, de-prioritized (stderr)
    if (response->total_tokens > 0) {
        debug_printf("\n[tokens: %d total", response->total_tokens);
        if (response->prompt_tokens > 0 && response->completion_tokens > 0) {
            debug_printf(" (%d prompt + %d completion)", 
                        response->prompt_tokens, response->completion_tokens);
        }
        debug_printf("]\n");
    }
}

void cleanup_parsed_response(ParsedResponse *response) {
    if (response) {
        if (response->thinking_content) {
            free(response->thinking_content);
            response->thinking_content = NULL;
        }
        if (response->response_content) {
            free(response->response_content);
            response->response_content = NULL;
        }
    }
}