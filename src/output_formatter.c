#include "output_formatter.h"
#include "debug_output.h"
#include "model_capabilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>


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

// Filter out raw tool call markup from response content to prevent displaying it to users
static void filter_tool_call_markup(char *str) {
    if (!str) return;
    
    char *src = str;
    char *dst = str;
    
    while (*src) {
        // Look for <tool_call> start tag
        if (strncmp(src, "<tool_call>", 11) == 0) {
            // Find the corresponding </tool_call> end tag
            const char *end_tag = strstr(src, "</tool_call>");
            if (end_tag) {
                // Skip everything from <tool_call> to </tool_call> inclusive
                src = (char*)(end_tag + 12); // 12 = strlen("</tool_call>")
                continue;
            } else {
                // Malformed - no closing tag found, just copy this character
                *dst++ = *src++;
            }
        } else {
            // Regular character, copy it
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

// Global model registry - initialized once
static ModelRegistry* g_model_registry = NULL;

ModelRegistry* get_model_registry() {
    if (!g_model_registry) {
        g_model_registry = malloc(sizeof(ModelRegistry));
        if (g_model_registry) {
            if (init_model_registry(g_model_registry) == 0) {
                // Register all known models
                register_qwen_models(g_model_registry);
                register_deepseek_models(g_model_registry);
                register_gpt_models(g_model_registry);
                register_claude_models(g_model_registry);
                register_default_model(g_model_registry);
            }
        }
    }
    return g_model_registry;
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
                // Filter out raw tool call markup from response content
                filter_tool_call_markup(*response);
            }
        }
    } else {
        // No thinking tags, entire content is the response
        size_t content_len = strlen(content);
        *response = malloc(content_len + 1);
        if (*response) {
            strcpy(*response, content);
            // Filter out raw tool call markup from response content
            filter_tool_call_markup(*response);
        }
    }
}

int parse_api_response(const char *json_response, ParsedResponse *result) {
    if (!json_response || !result) {
        return -1;
    }
    
    // Try to extract model name from response
    char *model_name = extract_json_string(json_response, "model");
    
    // Call the new function with model name
    int ret = parse_api_response_with_model(json_response, model_name, result);
    
    if (model_name) {
        free(model_name);
    }
    
    return ret;
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

int parse_anthropic_response(const char *json_response, ParsedResponse *result) {
    if (!json_response || !result) {
        return -1;
    }
    
    // Initialize result
    result->thinking_content = NULL;
    result->response_content = NULL;
    result->prompt_tokens = -1;
    result->completion_tokens = -1;
    result->total_tokens = -1;
    
    // Anthropic response format has content array
    // Look for "content": [{"type": "text", "text": "..."}]
    const char *content_array = strstr(json_response, "\"content\":");
    if (!content_array) {
        return -1;
    }
    
    // Find the text content within the array
    const char *text_pos = strstr(content_array, "\"text\":");
    if (text_pos) {
        char *raw_content = extract_json_string(text_pos, "text");
        if (raw_content) {
            // Unescape JSON strings
            unescape_json_string(raw_content);
            
            // Separate thinking from response (same as OpenAI)
            separate_thinking_and_response(raw_content, &result->thinking_content, &result->response_content);
            free(raw_content);
        }
    }
    
    // Extract token usage from Anthropic response
    const char *usage_start = strstr(json_response, "\"usage\":");
    if (usage_start) {
        result->prompt_tokens = extract_json_int(usage_start, "input_tokens");
        result->completion_tokens = extract_json_int(usage_start, "output_tokens");
        // Anthropic doesn't provide total_tokens, so calculate it
        if (result->prompt_tokens > 0 && result->completion_tokens > 0) {
            result->total_tokens = result->prompt_tokens + result->completion_tokens;
        }
    }
    
    return 0;
}

int parse_api_response_with_model(const char *json_response, const char *model_name, ParsedResponse *result) {
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
        
        // Use model-specific processing if available
        ModelRegistry* registry = get_model_registry();
        if (registry && model_name) {
            ModelCapabilities* model = detect_model_capabilities(registry, model_name);
            if (model && model->process_response) {
                int ret = model->process_response(raw_content, result);
                free(raw_content);
                if (ret != 0) {
                    return -1;
                }
            } else {
                // Fallback to default processing
                separate_thinking_and_response(raw_content, &result->thinking_content, &result->response_content);
                free(raw_content);
            }
        } else {
            // No model registry or model name, use default processing
            separate_thinking_and_response(raw_content, &result->thinking_content, &result->response_content);
            free(raw_content);
        }
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

// Additional ANSI codes for improved formatting
#define ANSI_CYAN    "\033[36m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BOLD    "\033[1m"

// Visual separators
#define SEPARATOR_LIGHT "────────────────────────────────────────"
#define SEPARATOR_HEAVY "════════════════════════════════════════"

// Global state for output grouping
static bool system_info_group_active = false;
static bool tool_execution_group_active = false;

// Helper function to create formatted strings - declare before use
static char* sprintf_allocated(const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    // Calculate required size
    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    if (size < 0) {
        va_end(args);
        return NULL;
    }
    
    // Allocate and format
    char* result = malloc(size + 1);
    if (result) {
        vsnprintf(result, size + 1, format, args);
    }
    
    va_end(args);
    return result;
}

void print_formatted_response_improved(const ParsedResponse *response) {
    if (!response) {
        return;
    }
    
    // Clear visual separator before main response
    printf("\n" ANSI_BOLD SEPARATOR_HEAVY ANSI_RESET "\n");
    
    // Print thinking content in dim gray if present (unchanged behavior)
    if (response->thinking_content) {
        printf(ANSI_DIM ANSI_GRAY "%s" ANSI_RESET "\n\n", response->thinking_content);
    }
    
    // Print the main response content prominently with clear separation
    if (response->response_content) {
        printf("%s\n", response->response_content);
    }
    
    // Add separator after main response to visually separate from system info
    printf("\n" ANSI_BOLD SEPARATOR_HEAVY ANSI_RESET "\n");
    
    // Print token usage in a dedicated system info section
    if (response->total_tokens > 0) {
        display_system_info_group_start();
        if (response->prompt_tokens > 0 && response->completion_tokens > 0) {
            char* usage_msg = sprintf_allocated("%d total (%d prompt + %d completion)", 
                response->total_tokens, response->prompt_tokens, response->completion_tokens);
            if (usage_msg) {
                log_system_info("Token Usage", usage_msg);
                free(usage_msg);
            }
        } else {
            char* usage_msg = sprintf_allocated("%d total", response->total_tokens);
            if (usage_msg) {
                log_system_info("Token Usage", usage_msg);
                free(usage_msg);
            }
        }
        display_system_info_group_end();
    }
}

void display_tool_execution_group_start(void) {
    if (!tool_execution_group_active) {
        printf("\n" ANSI_CYAN ANSI_BOLD "▼ Tool Execution" ANSI_RESET "\n");
        printf(ANSI_CYAN SEPARATOR_LIGHT ANSI_RESET "\n");
        tool_execution_group_active = true;
    }
}

void display_tool_execution_group_end(void) {
    if (tool_execution_group_active) {
        printf(ANSI_CYAN SEPARATOR_LIGHT ANSI_RESET "\n");
        printf(ANSI_CYAN ANSI_BOLD "▲ Tool Execution Complete" ANSI_RESET "\n\n");
        tool_execution_group_active = false;
    }
}

void log_tool_execution_improved(const char *tool_name, const char *arguments, bool success, const char *result) {
    if (!tool_name) return;
    
    // Skip internal todo tool logging
    if (strcmp(tool_name, "TodoWrite") == 0) {
        return;
    }
    
    // Print tool name and status
    if (success) {
        printf(ANSI_CYAN "  ✓ " ANSI_GREEN "%s" ANSI_RESET, tool_name);
    } else {
        printf(ANSI_CYAN "  ✗ " ANSI_RED "%s" ANSI_RESET, tool_name);
    }
    
    // Show arguments if provided (truncated for readability)
    if (arguments && strlen(arguments) > 0) {
        if (strlen(arguments) > 100) {
            printf(ANSI_DIM " (%.97s...)" ANSI_RESET, arguments);
        } else {
            printf(ANSI_DIM " (%s)" ANSI_RESET, arguments);
        }
    }
    
    printf("\n");
    
    // Show result/error for failures
    if (!success && result && strlen(result) > 0) {
        if (strlen(result) > 200) {
            printf(ANSI_RED "    Error: %.197s..." ANSI_RESET "\n", result);
        } else {
            printf(ANSI_RED "    Error: %s" ANSI_RESET "\n", result);
        }
    }
    
    fflush(stdout);
}

void display_system_info_group_start(void) {
    if (!system_info_group_active) {
        printf(ANSI_YELLOW ANSI_BOLD "▼ System Information" ANSI_RESET "\n");
        printf(ANSI_YELLOW SEPARATOR_LIGHT ANSI_RESET "\n");
        system_info_group_active = true;
    }
}

void display_system_info_group_end(void) {
    if (system_info_group_active) {
        printf(ANSI_YELLOW SEPARATOR_LIGHT ANSI_RESET "\n");
        printf(ANSI_YELLOW ANSI_BOLD "▲ System Information Complete" ANSI_RESET "\n\n");
        system_info_group_active = false;
    }
}

void log_system_info(const char *category, const char *message) {
    if (!category || !message) return;
    
    printf(ANSI_YELLOW "  %s:" ANSI_RESET " %s\n", category, message);
    fflush(stdout);
}