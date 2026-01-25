#include "output_formatter.h"
#include "debug_output.h"
#include "model_capabilities.h"
#include "../tools/todo_display.h"
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
                        accumulated_thinking = new_thinking;
                        strcat(accumulated_thinking, "\n");
                        strcat(accumulated_thinking, thinking->valuestring);
                    }
                    // If realloc fails, continue with existing content
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
                        accumulated_text = new_text;
                        strcat(accumulated_text, "\n");
                        strcat(accumulated_text, text->valuestring);
                    }
                    // If realloc fails, continue with existing content
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
                        result->thinking_content = merged;
                        strcat(result->thinking_content, "\n");
                        strcat(result->thinking_content, inner_thinking);
                    }
                    // If realloc fails, keep original thinking_content
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
    } else if (tool_name && strcmp(tool_name, "search_files") == 0) {
        // Search tools - show the pattern (what we're searching for) with path context
        if (pattern && cJSON_IsString(pattern)) {
            const char *pattern_val = cJSON_GetStringValue(pattern);
            const char *path_val = path && cJSON_IsString(path) ? cJSON_GetStringValue(path) : ".";
            // Format: "path → pattern" to show both where and what
            snprintf(summary, sizeof(summary), "%s → \"%s\"", path_val, pattern_val);
            cJSON_Delete(json);
            return strdup(summary);
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

    // Task/Todo tool specific parameters
    if (value == NULL) {
        cJSON *subject = cJSON_GetObjectItem(json, "subject");
        cJSON *taskId = cJSON_GetObjectItem(json, "taskId");
        cJSON *status = cJSON_GetObjectItem(json, "status");
        cJSON *content_field = cJSON_GetObjectItem(json, "content");

        if (subject && cJSON_IsString(subject)) {
            value = cJSON_GetStringValue(subject);
            label = "";
        } else if (taskId && cJSON_IsString(taskId)) {
            // For task updates, show both taskId and status if available
            const char *task_id_val = cJSON_GetStringValue(taskId);
            if (status && cJSON_IsString(status)) {
                const char *status_val = cJSON_GetStringValue(status);
                snprintf(summary, sizeof(summary), "#%s → %s", task_id_val, status_val);
                cJSON_Delete(json);
                return strdup(summary);
            } else {
                value = task_id_val;
                label = "#";
            }
        } else if (content_field && cJSON_IsString(content_field)) {
            value = cJSON_GetStringValue(content_field);
            label = "";
        }
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

    // For TodoWrite, show a meaningful summary of what tasks were written
    if (strcmp(tool_name, "TodoWrite") == 0) {
        char summary[128];
        memset(summary, 0, sizeof(summary));
        int task_count = 0;
        char first_task[64];
        memset(first_task, 0, sizeof(first_task));

        // Parse the todos array to extract summary info
        if (arguments) {
            cJSON *json = cJSON_Parse(arguments);
            if (json) {
                cJSON *todos = cJSON_GetObjectItem(json, "todos");
                if (todos && cJSON_IsArray(todos)) {
                    task_count = cJSON_GetArraySize(todos);

                    // Get first task's content/title for display
                    if (task_count > 0) {
                        cJSON *first = cJSON_GetArrayItem(todos, 0);
                        if (first) {
                            cJSON *content = cJSON_GetObjectItem(first, "content");
                            if (!content) content = cJSON_GetObjectItem(first, "title");
                            if (content && cJSON_IsString(content)) {
                                const char *text = cJSON_GetStringValue(content);
                                if (text) {
                                    size_t len = strlen(text);
                                    if (len <= 40) {
                                        snprintf(first_task, sizeof(first_task), "%s", text);
                                    } else {
                                        snprintf(first_task, sizeof(first_task), "%.37s...", text);
                                    }
                                }
                            }
                        }
                    }
                }
                cJSON_Delete(json);
            }
        }

        // Build the display summary
        if (task_count > 0 && first_task[0] != '\0') {
            if (task_count == 1) {
                snprintf(summary, sizeof(summary), "1 task: \"%s\"", first_task);
            } else {
                snprintf(summary, sizeof(summary), "%d tasks: \"%s\"%s",
                         task_count, first_task,
                         task_count > 1 ? ", ..." : "");
            }
        } else if (task_count > 0) {
            snprintf(summary, sizeof(summary), "%d task%s", task_count, task_count == 1 ? "" : "s");
        } else {
            snprintf(summary, sizeof(summary), "updated");
        }

        printf(ANSI_GREEN "✓" ANSI_RESET " TodoWrite" ANSI_DIM " (%s)" ANSI_RESET "\n\n", summary);
        fflush(stdout);
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

    // Print content with color - no box borders
    if (success) {
        printf(ANSI_GREEN "✓" ANSI_RESET " %s" ANSI_DIM "%s" ANSI_RESET "\n\n", tool_name, context);
    } else if (is_info_check) {
        printf(ANSI_YELLOW "◦" ANSI_RESET " %s" ANSI_DIM "%s" ANSI_RESET "\n\n", tool_name, context);
    } else {
        printf(ANSI_RED "✗" ANSI_RESET " %s" ANSI_DIM "%s" ANSI_RESET "\n", tool_name, context);
        // Show errors for actual failures
        if (result && strlen(result) > 0) {
            if (strlen(result) > 70) {
                printf(ANSI_RED "  └─ Error: %.67s..." ANSI_RESET "\n\n", result);
            } else {
                printf(ANSI_RED "  └─ Error: %s" ANSI_RESET "\n\n", result);
            }
        } else {
            printf("\n");
        }
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