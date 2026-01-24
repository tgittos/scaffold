#include "output_formatter.h"
#include "debug_output.h"
#include "model_capabilities.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// =============================================================================
// JSON Output Mode State
// =============================================================================

static bool g_json_output_mode = false;

void set_json_output_mode(bool enabled) {
    g_json_output_mode = enabled;
}

bool get_json_output_mode(void) {
    return g_json_output_mode;
}

// State for streaming display
static int streaming_first_chunk = 1;


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
        } 
        // Filter out memory tool JSON patterns like {"type": "write", "memory": {...}}
        else if (strncmp(src, "{\"type\":", 8) == 0) {
            // Check if this is a memory tool call pattern
            const char *memory_check = strstr(src, "\"memory\":");
            if (memory_check && memory_check < src + 100) { // Check within reasonable distance
                // Find the end of this JSON object by counting braces
                int brace_count = 0;
                const char *json_end = src;
                while (*json_end != '\0') {
                    if (*json_end == '{') brace_count++;
                    else if (*json_end == '}') {
                        brace_count--;
                        if (brace_count == 0) {
                            // Found the end of the JSON object
                            src = (char*)(json_end + 1);
                            // Skip trailing whitespace after the JSON
                            while (*src && (*src == ' ' || *src == '\n' || *src == '\r' || *src == '\t')) {
                                src++;
                            }
                            break;
                        }
                    }
                    json_end++;
                }
                continue;
            } else {
                // Not a memory tool call, copy the character
                *dst++ = *src++;
            }
        } else {
            // Regular character, copy it
            *dst++ = *src++;
        }
    }
    *dst = '\0';
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

    // Parse JSON properly to handle extended thinking format
    cJSON *root = cJSON_Parse(json_response);
    if (!root) {
        return -1;
    }

    // Get the content array
    cJSON *content_array = cJSON_GetObjectItem(root, "content");
    if (!content_array || !cJSON_IsArray(content_array)) {
        cJSON_Delete(root);
        return -1;
    }

    // Accumulate thinking and text content from multiple blocks
    char *accumulated_thinking = NULL;
    char *accumulated_text = NULL;

    // Iterate through content blocks
    cJSON *block = NULL;
    cJSON_ArrayForEach(block, content_array) {
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !cJSON_IsString(type)) {
            continue;
        }

        const char *type_str = type->valuestring;

        if (strcmp(type_str, "thinking") == 0) {
            // Extended thinking block - extract "thinking" field
            cJSON *thinking = cJSON_GetObjectItem(block, "thinking");
            if (thinking && cJSON_IsString(thinking) && thinking->valuestring) {
                if (accumulated_thinking == NULL) {
                    accumulated_thinking = strdup(thinking->valuestring);
                } else {
                    // Append to existing thinking (rare but handle it)
                    size_t old_len = strlen(accumulated_thinking);
                    size_t new_len = strlen(thinking->valuestring);
                    char *new_thinking = realloc(accumulated_thinking, old_len + new_len + 2);
                    if (new_thinking) {
                        strcat(new_thinking, "\n");
                        strcat(new_thinking, thinking->valuestring);
                        accumulated_thinking = new_thinking;
                    }
                }
            }
        } else if (strcmp(type_str, "text") == 0) {
            // Text block - extract "text" field
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text) && text->valuestring) {
                if (accumulated_text == NULL) {
                    accumulated_text = strdup(text->valuestring);
                } else {
                    // Append to existing text (rare but handle it)
                    size_t old_len = strlen(accumulated_text);
                    size_t new_len = strlen(text->valuestring);
                    char *new_text = realloc(accumulated_text, old_len + new_len + 2);
                    if (new_text) {
                        strcat(new_text, "\n");
                        strcat(new_text, text->valuestring);
                        accumulated_text = new_text;
                    }
                }
            }
        }
        // Ignore other block types (tool_use, etc.) for response parsing
    }

    // Set thinking content from extended thinking blocks
    result->thinking_content = accumulated_thinking;

    // For text content, also check for embedded <think> tags (legacy format)
    if (accumulated_text) {
        // Check if text contains <think> tags (legacy format)
        if (strstr(accumulated_text, "<think>") && strstr(accumulated_text, "</think>")) {
            char *inner_thinking = NULL;
            char *inner_response = NULL;
            separate_thinking_and_response(accumulated_text, &inner_thinking, &inner_response);

            // Merge any inner thinking with accumulated thinking
            if (inner_thinking) {
                if (result->thinking_content == NULL) {
                    result->thinking_content = inner_thinking;
                } else {
                    // Append inner thinking to existing
                    size_t old_len = strlen(result->thinking_content);
                    size_t new_len = strlen(inner_thinking);
                    char *merged = realloc(result->thinking_content, old_len + new_len + 2);
                    if (merged) {
                        strcat(merged, "\n");
                        strcat(merged, inner_thinking);
                        result->thinking_content = merged;
                    }
                    free(inner_thinking);
                }
            }

            result->response_content = inner_response;
            free(accumulated_text);
        } else {
            // No embedded thinking tags, use text as-is
            result->response_content = accumulated_text;
        }
    }

    // Extract token usage from Anthropic response
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *input_tokens = cJSON_GetObjectItem(usage, "input_tokens");
        cJSON *output_tokens = cJSON_GetObjectItem(usage, "output_tokens");

        if (input_tokens && cJSON_IsNumber(input_tokens)) {
            result->prompt_tokens = input_tokens->valueint;
        }
        if (output_tokens && cJSON_IsNumber(output_tokens)) {
            result->completion_tokens = output_tokens->valueint;
        }
        if (result->prompt_tokens > 0 && result->completion_tokens > 0) {
            result->total_tokens = result->prompt_tokens + result->completion_tokens;
        }
    }

    cJSON_Delete(root);
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

    // Parse JSON properly
    cJSON *root = cJSON_Parse(json_response);
    if (!root) {
        return -1;
    }

    // OpenAI format: choices[0].message.content
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
    if (!first_choice) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *message = cJSON_GetObjectItem(first_choice, "message");
    if (!message) {
        cJSON_Delete(root);
        return -1;
    }

    // Check for content field
    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!content) {
        // No content field - check for tool_calls (valid case)
        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls) {
            // Tool call response - extract usage and return success
            cJSON *usage = cJSON_GetObjectItem(root, "usage");
            if (usage) {
                cJSON *prompt = cJSON_GetObjectItem(usage, "prompt_tokens");
                cJSON *completion = cJSON_GetObjectItem(usage, "completion_tokens");
                cJSON *total = cJSON_GetObjectItem(usage, "total_tokens");
                if (prompt && cJSON_IsNumber(prompt)) result->prompt_tokens = prompt->valueint;
                if (completion && cJSON_IsNumber(completion)) result->completion_tokens = completion->valueint;
                if (total && cJSON_IsNumber(total)) result->total_tokens = total->valueint;
            }
            cJSON_Delete(root);
            return 0;
        }
        cJSON_Delete(root);
        return -1;
    }

    // Content can be null (for tool calls) or a string
    if (cJSON_IsNull(content)) {
        // Content is null - valid for tool calls, leave result fields as NULL
    } else if (cJSON_IsString(content) && content->valuestring) {
        // Content is a string - process it
        char *raw_content = strdup(content->valuestring);
        if (!raw_content) {
            cJSON_Delete(root);
            return -1;
        }

        // Use model-specific processing if available
        ModelRegistry* registry = get_model_registry();
        if (registry && model_name) {
            ModelCapabilities* model = detect_model_capabilities(registry, model_name);
            if (model && model->process_response) {
                int ret = model->process_response(raw_content, result);
                free(raw_content);
                if (ret != 0) {
                    cJSON_Delete(root);
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
        cJSON_Delete(root);
        return -1;
    }

    // Extract token usage
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *prompt = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *completion = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON *total = cJSON_GetObjectItem(usage, "total_tokens");
        if (prompt && cJSON_IsNumber(prompt)) result->prompt_tokens = prompt->valueint;
        if (completion && cJSON_IsNumber(completion)) result->completion_tokens = completion->valueint;
        if (total && cJSON_IsNumber(total)) result->total_tokens = total->valueint;
    }

    cJSON_Delete(root);
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


void print_formatted_response_improved(const ParsedResponse *response) {
    if (!response) {
        return;
    }

    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Print thinking content in dim gray if present (unchanged behavior)
    if (response->thinking_content) {
        printf(ANSI_DIM ANSI_GRAY "%s" ANSI_RESET "\n\n", response->thinking_content);
    }

    // Print the main response content prominently
    if (response->response_content) {
        printf("%s\n", response->response_content);

        // Print token usage on separate line, grouped with response
        if (response->total_tokens > 0) {
            if (response->prompt_tokens > 0 && response->completion_tokens > 0) {
                printf(ANSI_DIM "    └─ %d tokens (%d prompt + %d completion)\n" ANSI_RESET,
                       response->total_tokens, response->prompt_tokens, response->completion_tokens);
            } else {
                printf(ANSI_DIM "    └─ %d tokens\n" ANSI_RESET, response->total_tokens);
            }
        }

        printf("\n");
    }
}

void display_tool_execution_group_start(void) {
    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    if (!tool_execution_group_active) {
        // Professional header for tool execution section with proper width (80 chars total)
        printf(ANSI_CYAN "┌─ " ANSI_BOLD "Tool Execution" ANSI_RESET ANSI_CYAN " ─────────────────────────────────────────────────────────────┐" ANSI_RESET "\n");
        tool_execution_group_active = true;
    }
}

void display_tool_execution_group_end(void) {
    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    if (tool_execution_group_active) {
        printf(ANSI_CYAN "└──────────────────────────────────────────────────────────────────────────────┘" ANSI_RESET "\n\n");
        fflush(stdout);
        tool_execution_group_active = false;
    }
}

// Box width is 80 characters total: │ (1) + space (1) + content+padding (77) + │ (1)
#define TOOL_BOX_WIDTH 80
#define TOOL_BOX_CONTENT_WIDTH (TOOL_BOX_WIDTH - 3)  // 77 chars for content+padding

void print_tool_box_line(const char* format, ...) {
    if (format == NULL) return;

    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Format the content
    char content[512];
    va_list args;
    va_start(args, format);
    vsnprintf(content, sizeof(content), format, args);
    va_end(args);

    // Calculate content length (visible characters only)
    size_t content_len = strlen(content);

    // Print left border with space
    printf(ANSI_CYAN "│" ANSI_RESET " ");

    // Print content
    printf("%s", content);

    // Calculate padding needed (account for content width)
    int padding = TOOL_BOX_CONTENT_WIDTH - (int)content_len;
    if (padding < 0) padding = 0;

    // Print padding and right border
    printf("%*s" ANSI_CYAN "│" ANSI_RESET "\n", padding, "");
    fflush(stdout);
}

static bool is_informational_check(const char *tool_name, const char *arguments) {
    if (!tool_name || !arguments) return false;

    // Common informational commands that are expected to sometimes "fail"
    if (strcmp(tool_name, "shell_execute") == 0 || strcmp(tool_name, "shell") == 0) {
        // Version checks, which commands, etc.
        if (strstr(arguments, "--version") || strstr(arguments, "which ") ||
            strstr(arguments, "command -v") || strstr(arguments, "type ")) {
            return true;
        }
    }

    return false;
}

// Maximum display length for argument values
#define ARG_DISPLAY_MAX_LEN 50

// Extract a formatted argument summary from JSON arguments
// Returns a malloc'd string that must be freed by caller, or NULL
static char* extract_arg_summary(const char *tool_name, const char *arguments) {
    if (!arguments || strlen(arguments) == 0) {
        return NULL;
    }

    cJSON *json = cJSON_Parse(arguments);
    if (!json) {
        return NULL;
    }

    char summary[256];
    memset(summary, 0, sizeof(summary));
    const char *value = NULL;
    const char *label = NULL;

    // Priority order for display - look for the most meaningful parameter
    // File operations
    cJSON *path = cJSON_GetObjectItem(json, "path");
    cJSON *file_path = cJSON_GetObjectItem(json, "file_path");
    cJSON *directory_path = cJSON_GetObjectItem(json, "directory_path");
    // Shell/command operations
    cJSON *command = cJSON_GetObjectItem(json, "command");
    // Web operations
    cJSON *url = cJSON_GetObjectItem(json, "url");
    // Search operations
    cJSON *query = cJSON_GetObjectItem(json, "query");
    cJSON *pattern = cJSON_GetObjectItem(json, "pattern");
    // Memory operations
    cJSON *key = cJSON_GetObjectItem(json, "key");
    // Vector DB operations
    cJSON *collection = cJSON_GetObjectItem(json, "collection");
    cJSON *text = cJSON_GetObjectItem(json, "text");

    // Determine what to display based on tool name and available args
    if (tool_name && (strcmp(tool_name, "shell") == 0 || strcmp(tool_name, "shell_execute") == 0)) {
        // Shell commands - show the command
        if (command && cJSON_IsString(command)) {
            value = cJSON_GetStringValue(command);
            label = "";  // No label needed, command is self-explanatory
        }
    } else if (tool_name && strstr(tool_name, "write") != NULL) {
        // Write operations - show path, indicate content exists
        if (path && cJSON_IsString(path)) {
            value = cJSON_GetStringValue(path);
            label = "";
        } else if (file_path && cJSON_IsString(file_path)) {
            value = cJSON_GetStringValue(file_path);
            label = "";
        }
    } else if (path && cJSON_IsString(path)) {
        value = cJSON_GetStringValue(path);
        label = "";
    } else if (file_path && cJSON_IsString(file_path)) {
        value = cJSON_GetStringValue(file_path);
        label = "";
    } else if (directory_path && cJSON_IsString(directory_path)) {
        value = cJSON_GetStringValue(directory_path);
        label = "";
    } else if (command && cJSON_IsString(command)) {
        value = cJSON_GetStringValue(command);
        label = "";
    } else if (url && cJSON_IsString(url)) {
        value = cJSON_GetStringValue(url);
        label = "";
    } else if (query && cJSON_IsString(query)) {
        value = cJSON_GetStringValue(query);
        label = "query: ";
    } else if (pattern && cJSON_IsString(pattern)) {
        value = cJSON_GetStringValue(pattern);
        label = "pattern: ";
    } else if (key && cJSON_IsString(key)) {
        value = cJSON_GetStringValue(key);
        label = "key: ";
    } else if (collection && cJSON_IsString(collection)) {
        value = cJSON_GetStringValue(collection);
        label = "collection: ";
    } else if (text && cJSON_IsString(text)) {
        value = cJSON_GetStringValue(text);
        label = "text: ";
    }

    if (value && strlen(value) > 0) {
        size_t value_len = strlen(value);
        if (value_len <= ARG_DISPLAY_MAX_LEN) {
            snprintf(summary, sizeof(summary), "%s%s", label, value);
        } else {
            // Truncate with ellipsis
            snprintf(summary, sizeof(summary), "%s%.47s...", label, value);
        }
    }

    cJSON_Delete(json);

    if (strlen(summary) > 0) {
        return strdup(summary);
    }
    return NULL;
}

void log_tool_execution_improved(const char *tool_name, const char *arguments, bool success, const char *result) {
    if (!tool_name) return;

    // In JSON mode, terminal display is suppressed (tool results handled via json_output_tool_result)
    if (g_json_output_mode) {
        return;
    }

    // Skip internal todo tool logging
    if (strcmp(tool_name, "TodoWrite") == 0) {
        return;
    }

    // Check if this is an informational check rather than a real failure
    bool is_info_check = !success && is_informational_check(tool_name, arguments);

    // Extract argument summary for display
    char *arg_summary = extract_arg_summary(tool_name, arguments);
    char context[128];
    memset(context, 0, sizeof(context));

    if (arg_summary && strlen(arg_summary) > 0) {
        snprintf(context, sizeof(context), " (%s)", arg_summary);
    }

    if (arg_summary) {
        free(arg_summary);
    }

    // Calculate visible length: icon (1) + space (1) + tool_name + context
    size_t visible_len = strlen(tool_name) + strlen(context) + 2;

    // Print left border
    printf(ANSI_CYAN "│" ANSI_RESET " ");

    // Print content with color
    if (success) {
        printf(ANSI_GREEN "✓" ANSI_RESET " %s" ANSI_DIM "%s" ANSI_RESET, tool_name, context);
    } else if (is_info_check) {
        printf(ANSI_YELLOW "◦" ANSI_RESET " %s" ANSI_DIM "%s" ANSI_RESET, tool_name, context);
    } else {
        printf(ANSI_RED "✗" ANSI_RESET " %s" ANSI_DIM "%s" ANSI_RESET, tool_name, context);
    }

    // Padding and right border
    int padding = TOOL_BOX_CONTENT_WIDTH - (int)visible_len;
    if (padding < 0) padding = 0;
    printf("%*s" ANSI_CYAN "│" ANSI_RESET "\n", padding, "");

    // Show errors for actual failures with proper indentation
    if (!success && !is_info_check && result && strlen(result) > 0) {
        char error_line[128];
        memset(error_line, 0, sizeof(error_line));
        if (strlen(result) > 60) {
            snprintf(error_line, sizeof(error_line), "  └─ Error: %.57s...", result);
        } else {
            snprintf(error_line, sizeof(error_line), "  └─ Error: %s", result);
        }
        size_t err_visible_len = strlen(error_line);
        int err_padding = TOOL_BOX_CONTENT_WIDTH - (int)err_visible_len;
        if (err_padding < 0) err_padding = 0;
        printf(ANSI_CYAN "│" ANSI_RESET " " ANSI_RED "%s" ANSI_RESET "%*s" ANSI_CYAN "│" ANSI_RESET "\n",
               error_line, err_padding, "");
    }

    fflush(stdout);
}

void display_system_info_group_start(void) {
    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    if (!system_info_group_active) {
        printf("\n" ANSI_YELLOW ANSI_BOLD "▼ System Information" ANSI_RESET "\n");
        printf(ANSI_YELLOW SEPARATOR_LIGHT ANSI_RESET "\n");
        system_info_group_active = true;
    }
}

void display_system_info_group_end(void) {
    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    if (system_info_group_active) {
        system_info_group_active = false;
    }
}

void log_system_info(const char *category, const char *message) {
    if (!category || !message) return;

    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    printf(ANSI_YELLOW "  %s:" ANSI_RESET " %s\n", category, message);
    fflush(stdout);
}

void cleanup_output_formatter(void) {
    if (g_model_registry) {
        cleanup_model_registry(g_model_registry);
        free(g_model_registry);
        g_model_registry = NULL;
    }
}

// =============================================================================
// Streaming Display Functions
// =============================================================================

void display_streaming_init(void) {
    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Display thinking indicator while waiting for first chunk
    streaming_first_chunk = 1;
    fprintf(stdout, "\033[36m•\033[0m ");
    fflush(stdout);
}

void display_streaming_text(const char* text, size_t len) {
    if (text == NULL || len == 0) {
        return;
    }

    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Clear thinking indicator on first chunk
    if (streaming_first_chunk) {
        fprintf(stdout, "\r\033[K");
        streaming_first_chunk = 0;
    }
    fwrite(text, 1, len, stdout);
    fflush(stdout);
}

void display_streaming_thinking(const char* text, size_t len) {
    if (text == NULL || len == 0) {
        return;
    }

    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Clear thinking indicator on first chunk
    if (streaming_first_chunk) {
        fprintf(stdout, "\r\033[K");
        streaming_first_chunk = 0;
    }
    // Display in dimmed gray style
    printf(ANSI_DIM ANSI_GRAY);
    fwrite(text, 1, len, stdout);
    printf(ANSI_RESET);
    fflush(stdout);
}

void display_streaming_tool_start(const char* tool_name) {
    if (tool_name == NULL) {
        return;
    }

    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Clear thinking indicator on first chunk
    if (streaming_first_chunk) {
        fprintf(stdout, "\r\033[K");
        streaming_first_chunk = 0;
    }
    // No header displayed - tool execution box will show tool info
    (void)tool_name;
    fflush(stdout);
}

void display_streaming_complete(int input_tokens, int output_tokens) {
    // In JSON mode, terminal display is suppressed
    if (g_json_output_mode) {
        return;
    }

    // Final newline
    printf("\n");

    // Display token usage if available
    if (input_tokens > 0 || output_tokens > 0) {
        int total_tokens = input_tokens + output_tokens;
        printf(ANSI_DIM "    └─ %d tokens", total_tokens);
        if (input_tokens > 0 && output_tokens > 0) {
            printf(" (%d prompt + %d completion)", input_tokens, output_tokens);
        }
        printf(ANSI_RESET "\n");
    }

    fflush(stdout);
}

void display_streaming_error(const char* error) {
    if (error == NULL) {
        return;
    }
    // Clear thinking indicator if still showing
    if (streaming_first_chunk) {
        fprintf(stdout, "\r\033[K");
        streaming_first_chunk = 0;
    }
    fprintf(stderr, "\n" ANSI_RED "Error: %s" ANSI_RESET "\n", error);
    fflush(stderr);
}