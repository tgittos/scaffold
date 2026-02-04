#include "output_formatter.h"
#include "json_output.h"
#include "../util/debug_output.h"
#include "../llm/model_capabilities.h"
#include "../util/interrupt.h"
#include "../tools/todo_display.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static bool g_json_output_mode = false;

void set_json_output_mode(bool enabled) {
    g_json_output_mode = enabled;
}

bool get_json_output_mode(void) {
    return g_json_output_mode;
}

static int streaming_first_chunk = 1;


static char *extract_json_string(const char *json, const char *key) {
    if (!json || !key) {
        return NULL;
    }

    char pattern[256];
    memset(pattern, 0, sizeof(pattern));  /* Zero-initialize for Valgrind */
    int ret = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (ret < 0 || ret >= (int)sizeof(pattern)) {
        return NULL;
    }

    const char *key_pos = strstr(json, pattern);
    if (!key_pos) {
        return NULL;
    }

    const char *start = key_pos + strlen(pattern);

    while (*start && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) {
        start++;
    }

    if (*start != '"') {
        return NULL;
    }
    start++;

    const char *end = start;

    while (*end) {
        if (*end == '"') {
            break;
        } else if (*end == '\\' && *(end + 1)) {
            end += 2;
        } else {
            end++;
        }
    }

    if (*end != '"') {
        return NULL;
    }

    if (end < start) {
        return NULL;  /* Invalid range */
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

/* Strips <tool_call> XML blocks and memory-tool JSON patterns from response
 * text so raw markup never reaches the terminal display. */
static void filter_tool_call_markup(char *str) {
    if (!str) return;

    char *src = str;
    char *dst = str;

    while (*src) {
        if (strncmp(src, "<tool_call>", 11) == 0) {
            const char *end_tag = strstr(src, "</tool_call>");
            if (end_tag) {
                src = (char*)(end_tag + 12);
                continue;
            } else {
                *dst++ = *src++;
            }
        }
        else if (strncmp(src, "{\"type\":", 8) == 0) {
            const char *memory_check = strstr(src, "\"memory\":");
            if (memory_check && memory_check < src + 100) {
                int brace_count = 0;
                const char *json_end = src;
                while (*json_end != '\0') {
                    if (*json_end == '{') brace_count++;
                    else if (*json_end == '}') {
                        brace_count--;
                        if (brace_count == 0) {
                            src = (char*)(json_end + 1);
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
                *dst++ = *src++;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static ModelRegistry* g_model_registry = NULL;

ModelRegistry* get_model_registry() {
    if (!g_model_registry) {
        g_model_registry = malloc(sizeof(ModelRegistry));
        if (g_model_registry) {
            if (init_model_registry(g_model_registry) == 0) {
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

    const char *think_start = strstr(content, "<think>");
    const char *think_end = strstr(content, "</think>");

    if (think_start && think_end && think_end > think_start) {
        think_start += 7;
        size_t think_len = (size_t)(think_end - think_start);
        *thinking = malloc(think_len + 1);
        if (*thinking) {
            memcpy(*thinking, think_start, think_len);
            (*thinking)[think_len] = '\0';
        }

        const char *response_start = think_end + 8;
        while (*response_start && (*response_start == ' ' || *response_start == '\t' ||
               *response_start == '\n' || *response_start == '\r')) {
            response_start++;
        }

        if (*response_start) {
            size_t response_len = strlen(response_start);
            *response = malloc(response_len + 1);
            if (*response) {
                strcpy(*response, response_start);
                filter_tool_call_markup(*response);
            }
        }
    } else {
        size_t content_len = strlen(content);
        *response = malloc(content_len + 1);
        if (*response) {
            strcpy(*response, content);
            filter_tool_call_markup(*response);
        }
    }
}

int parse_api_response(const char *json_response, ParsedResponse *result) {
    if (!json_response || !result) {
        return -1;
    }

    char *model_name = extract_json_string(json_response, "model");
    int ret = parse_api_response_with_model(json_response, model_name, result);

    if (model_name) {
        free(model_name);
    }

    return ret;
}

int parse_anthropic_response(const char *json_response, ParsedResponse *result) {
    if (!json_response || !result) {
        return -1;
    }

    result->thinking_content = NULL;
    result->response_content = NULL;
    result->prompt_tokens = -1;
    result->completion_tokens = -1;
    result->total_tokens = -1;

    cJSON *root = cJSON_Parse(json_response);
    if (!root) {
        return -1;
    }

    cJSON *content_array = cJSON_GetObjectItem(root, "content");
    if (!content_array || !cJSON_IsArray(content_array)) {
        cJSON_Delete(root);
        return -1;
    }

    /* Anthropic responses may contain multiple thinking/text content blocks */
    char *accumulated_thinking = NULL;
    char *accumulated_text = NULL;

    cJSON *block = NULL;
    cJSON_ArrayForEach(block, content_array) {
        cJSON *type = cJSON_GetObjectItem(block, "type");
        if (!type || !cJSON_IsString(type)) {
            continue;
        }

        const char *type_str = type->valuestring;

        if (strcmp(type_str, "thinking") == 0) {
            cJSON *thinking = cJSON_GetObjectItem(block, "thinking");
            if (thinking && cJSON_IsString(thinking) && thinking->valuestring) {
                if (accumulated_thinking == NULL) {
                    accumulated_thinking = strdup(thinking->valuestring);
                } else {
                    size_t old_len = strlen(accumulated_thinking);
                    size_t new_len = strlen(thinking->valuestring);
                    char *new_thinking = realloc(accumulated_thinking, old_len + new_len + 2);
                    if (new_thinking) {
                        accumulated_thinking = new_thinking;
                        strcat(accumulated_thinking, "\n");
                        strcat(accumulated_thinking, thinking->valuestring);
                    }
                }
            }
        } else if (strcmp(type_str, "text") == 0) {
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (text && cJSON_IsString(text) && text->valuestring) {
                if (accumulated_text == NULL) {
                    accumulated_text = strdup(text->valuestring);
                } else {
                    size_t old_len = strlen(accumulated_text);
                    size_t new_len = strlen(text->valuestring);
                    char *new_text = realloc(accumulated_text, old_len + new_len + 2);
                    if (new_text) {
                        accumulated_text = new_text;
                        strcat(accumulated_text, "\n");
                        strcat(accumulated_text, text->valuestring);
                    }
                }
            }
        }
    }

    result->thinking_content = accumulated_thinking;

    /* Some models embed <think> tags in text instead of using dedicated thinking blocks */
    if (accumulated_text) {
        if (strstr(accumulated_text, "<think>") && strstr(accumulated_text, "</think>")) {
            char *inner_thinking = NULL;
            char *inner_response = NULL;
            separate_thinking_and_response(accumulated_text, &inner_thinking, &inner_response);

            if (inner_thinking) {
                if (result->thinking_content == NULL) {
                    result->thinking_content = inner_thinking;
                } else {
                    size_t old_len = strlen(result->thinking_content);
                    size_t new_len = strlen(inner_thinking);
                    char *merged = realloc(result->thinking_content, old_len + new_len + 2);
                    if (merged) {
                        result->thinking_content = merged;
                        strcat(result->thinking_content, "\n");
                        strcat(result->thinking_content, inner_thinking);
                    }
                    free(inner_thinking);
                }
            }

            result->response_content = inner_response;
            free(accumulated_text);
        } else {
            result->response_content = accumulated_text;
        }
    }

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

    result->thinking_content = NULL;
    result->response_content = NULL;
    result->prompt_tokens = -1;
    result->completion_tokens = -1;
    result->total_tokens = -1;

    cJSON *root = cJSON_Parse(json_response);
    if (!root) {
        return -1;
    }

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

    cJSON *content = cJSON_GetObjectItem(message, "content");
    if (!content) {
        /* Tool-call-only responses have no content field */
        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls) {
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

    if (cJSON_IsNull(content)) {
        /* null content is valid for tool-call-only responses */
    } else if (cJSON_IsString(content) && content->valuestring) {
        char *raw_content = strdup(content->valuestring);
        if (!raw_content) {
            cJSON_Delete(root);
            return -1;
        }

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
                separate_thinking_and_response(raw_content, &result->thinking_content, &result->response_content);
                free(raw_content);
            }
        } else {
            separate_thinking_and_response(raw_content, &result->thinking_content, &result->response_content);
            free(raw_content);
        }
    } else {
        cJSON_Delete(root);
        return -1;
    }

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


static bool system_info_group_active = false;


void print_formatted_response_improved(const ParsedResponse *response) {
    if (!response) {
        return;
    }

    if (g_json_output_mode) {
        return;
    }

    if (response->thinking_content) {
        printf(TERM_DIM TERM_GRAY "%s" TERM_RESET "\n\n", response->thinking_content);
    }

    if (response->response_content) {
        printf("%s\n", response->response_content);

        if (response->total_tokens > 0) {
            if (response->prompt_tokens > 0 && response->completion_tokens > 0) {
                printf(TERM_DIM "    └─ %d tokens (%d prompt + %d completion)\n" TERM_RESET,
                       response->total_tokens, response->prompt_tokens, response->completion_tokens);
            } else {
                printf(TERM_DIM "    └─ %d tokens\n" TERM_RESET, response->total_tokens);
            }
        }

        printf("\n");
    }
}


static bool is_informational_check(const char *tool_name, const char *arguments) {
    if (!tool_name || !arguments) return false;

    /* Common informational commands that are expected to sometimes "fail" */
    if (strcmp(tool_name, "shell_execute") == 0 || strcmp(tool_name, "shell") == 0) {
        /* Version checks, which commands, etc. */
        if (strstr(arguments, "--version") || strstr(arguments, "which ") ||
            strstr(arguments, "command -v") || strstr(arguments, "type ")) {
            return true;
        }
    }

    return false;
}

#define ARG_DISPLAY_MAX_LEN 50

char* extract_arg_summary(const char *tool_name, const char *arguments) {
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

    cJSON *path = cJSON_GetObjectItem(json, "path");
    cJSON *file_path = cJSON_GetObjectItem(json, "file_path");
    cJSON *directory_path = cJSON_GetObjectItem(json, "directory_path");
    cJSON *command = cJSON_GetObjectItem(json, "command");
    cJSON *url = cJSON_GetObjectItem(json, "url");
    cJSON *query = cJSON_GetObjectItem(json, "query");
    cJSON *pattern = cJSON_GetObjectItem(json, "pattern");
    cJSON *key = cJSON_GetObjectItem(json, "key");
    cJSON *collection = cJSON_GetObjectItem(json, "collection");
    cJSON *text = cJSON_GetObjectItem(json, "text");

    if (tool_name && (strcmp(tool_name, "shell") == 0 || strcmp(tool_name, "shell_execute") == 0)) {
        if (command && cJSON_IsString(command)) {
            value = cJSON_GetStringValue(command);
            label = "";
        }
    } else if (tool_name && strcmp(tool_name, "search_files") == 0) {
        cJSON *search_pattern = cJSON_GetObjectItem(json, "pattern");
        if (search_pattern && cJSON_IsString(search_pattern)) {
            const char *pattern_val = cJSON_GetStringValue(search_pattern);
            const char *path_val = path && cJSON_IsString(path) ? cJSON_GetStringValue(path) : ".";
            size_t pattern_len = strlen(pattern_val);
            if (pattern_len <= ARG_DISPLAY_MAX_LEN - 10) {
                snprintf(summary, sizeof(summary), "%s → /%s/", path_val, pattern_val);
            } else {
                snprintf(summary, sizeof(summary), "%s → /%.37s.../", path_val, pattern_val);
            }
            cJSON_Delete(json);
            return strdup(summary);
        }
    } else if (tool_name && strstr(tool_name, "write") != NULL) {
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

    if (value == NULL) {
        cJSON *subject = cJSON_GetObjectItem(json, "subject");
        cJSON *taskId = cJSON_GetObjectItem(json, "taskId");
        cJSON *status = cJSON_GetObjectItem(json, "status");
        cJSON *content_field = cJSON_GetObjectItem(json, "content");

        if (subject && cJSON_IsString(subject)) {
            value = cJSON_GetStringValue(subject);
            label = "";
        } else if (taskId && cJSON_IsString(taskId)) {
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

    if (g_json_output_mode) {
        return;
    }

    /* If interrupted, always show as failure */
    bool was_interrupted = interrupt_pending();
    if (was_interrupted) {
        success = false;
    }

    if (strcmp(tool_name, "TodoWrite") == 0 && !was_interrupted) {
        char summary[128];
        memset(summary, 0, sizeof(summary));
        int task_count = 0;
        char first_task[64];
        memset(first_task, 0, sizeof(first_task));

        if (arguments) {
            cJSON *json = cJSON_Parse(arguments);
            if (json) {
                cJSON *todos = cJSON_GetObjectItem(json, "todos");
                if (todos && cJSON_IsArray(todos)) {
                    task_count = cJSON_GetArraySize(todos);

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

        printf(TERM_GREEN "✓" TERM_RESET " TodoWrite" TERM_DIM " (%s)" TERM_RESET "\n\n", summary);
        fflush(stdout);
        return;
    }

    bool is_info_check = !success && is_informational_check(tool_name, arguments);
    char *arg_summary = extract_arg_summary(tool_name, arguments);
    char context[128];
    memset(context, 0, sizeof(context));

    if (arg_summary && strlen(arg_summary) > 0) {
        snprintf(context, sizeof(context), " (%s)", arg_summary);
    }

    if (arg_summary) {
        free(arg_summary);
    }

    if (success) {
        printf(TERM_GREEN "✓" TERM_RESET " %s" TERM_DIM "%s" TERM_RESET "\n\n", tool_name, context);
    } else if (is_info_check) {
        printf(TERM_YELLOW "◦" TERM_RESET " %s" TERM_DIM "%s" TERM_RESET "\n\n", tool_name, context);
    } else {
        printf(TERM_RED "✗" TERM_RESET " %s" TERM_DIM "%s" TERM_RESET "\n", tool_name, context);
        if (result && strlen(result) > 0) {
            if (strlen(result) > 70) {
                printf(TERM_RED "  └─ Error: %.67s..." TERM_RESET "\n\n", result);
            } else {
                printf(TERM_RED "  └─ Error: %s" TERM_RESET "\n\n", result);
            }
        } else {
            printf("\n");
        }
    }

    fflush(stdout);
}

void display_system_info_group_start(void) {
    if (g_json_output_mode) {
        return;
    }

    if (!system_info_group_active) {
        printf("\n" TERM_YELLOW TERM_BOLD "▼ System Information" TERM_RESET "\n");
        printf(TERM_YELLOW TERM_SEP_LIGHT_40 TERM_RESET "\n");
        system_info_group_active = true;
    }
}

void display_system_info_group_end(void) {
    if (g_json_output_mode) {
        return;
    }

    if (system_info_group_active) {
        system_info_group_active = false;
    }
}

void log_system_info(const char *category, const char *message) {
    if (!category || !message) return;

    if (g_json_output_mode) {
        return;
    }

    printf(TERM_YELLOW "  %s:" TERM_RESET " %s\n", category, message);
    fflush(stdout);
}

void cleanup_output_formatter(void) {
    if (g_model_registry) {
        cleanup_model_registry(g_model_registry);
        free(g_model_registry);
        g_model_registry = NULL;
    }
}

void display_streaming_init(void) {
    if (g_json_output_mode) {
        return;
    }

    streaming_first_chunk = 1;
    fprintf(stdout, TERM_CYAN TERM_SYM_ACTIVE TERM_RESET " ");
    fflush(stdout);
}

void display_streaming_text(const char* text, size_t len) {
    if (text == NULL || len == 0) {
        return;
    }

    if (g_json_output_mode) {
        return;
    }

    if (streaming_first_chunk) {
        fprintf(stdout, TERM_CLEAR_LINE);
        streaming_first_chunk = 0;
    }
    fwrite(text, 1, len, stdout);
    fflush(stdout);
}

void display_streaming_thinking(const char* text, size_t len) {
    if (text == NULL || len == 0) {
        return;
    }

    if (g_json_output_mode) {
        return;
    }

    if (streaming_first_chunk) {
        fprintf(stdout, TERM_CLEAR_LINE);
        streaming_first_chunk = 0;
    }
    printf(TERM_DIM TERM_GRAY);
    fwrite(text, 1, len, stdout);
    printf(TERM_RESET);
    fflush(stdout);
}

void display_streaming_tool_start(const char* tool_name) {
    if (tool_name == NULL) {
        return;
    }

    if (g_json_output_mode) {
        return;
    }

    if (streaming_first_chunk) {
        fprintf(stdout, TERM_CLEAR_LINE);
        streaming_first_chunk = 0;
    }
    (void)tool_name; /* Tool info shown by log_tool_execution_improved instead */
    fflush(stdout);
}

void display_streaming_complete(int input_tokens, int output_tokens) {
    if (g_json_output_mode) {
        return;
    }

    if (input_tokens > 0 || output_tokens > 0) {
        int total_tokens = input_tokens + output_tokens;
        printf("\n" TERM_DIM "    └─ %d tokens", total_tokens);
        if (input_tokens > 0 && output_tokens > 0) {
            printf(" (%d prompt + %d completion)", input_tokens, output_tokens);
        }
        printf(TERM_RESET "\n");
    }

    fflush(stdout);
}

void display_streaming_error(const char* error) {
    if (error == NULL) {
        return;
    }
    if (streaming_first_chunk) {
        fprintf(stdout, TERM_CLEAR_LINE);
        streaming_first_chunk = 0;
    }
    fprintf(stderr, "\n" TERM_RED "Error: %s" TERM_RESET "\n", error);
    fflush(stderr);
}

void display_message_notification(int count) {
    if (g_json_output_mode) {
        return;
    }

    if (count <= 0) {
        return;
    }

    fprintf(stdout, "\n" TERM_CLEAR_LINE TERM_YELLOW TERM_SYM_ACTIVE " " TERM_RESET);
    if (count == 1) {
        fprintf(stdout, TERM_YELLOW "1 new message" TERM_RESET "\n\n");
    } else {
        fprintf(stdout, TERM_YELLOW "%d new messages" TERM_RESET "\n\n", count);
    }
    fflush(stdout);
}

void display_message_notification_clear(void) {
    if (g_json_output_mode) {
        return;
    }

    fprintf(stdout, TERM_CLEAR_LINE);
    fflush(stdout);
}

/*
 * ApprovalResult enum values from policy/approval_gate.h.
 * We maintain a local copy of result names here to avoid creating a dependency
 * on the entire approval_gate module from output_formatter. If the enum changes,
 * this array must be updated as well.
 */
static const char *APPROVAL_RESULT_NAMES[] = {
    "allowed",              /* APPROVAL_ALLOWED = 0 */
    "denied",               /* APPROVAL_DENIED = 1 */
    "always",               /* APPROVAL_ALLOWED_ALWAYS = 2 */
    "aborted",              /* APPROVAL_ABORTED = 3 */
    "rate-limited",         /* APPROVAL_RATE_LIMITED = 4 */
    "no-tty"                /* APPROVAL_NON_INTERACTIVE_DENIED = 5 */
};
#define APPROVAL_RESULT_COUNT 6

void log_subagent_approval(const char *subagent_id,
                           const char *tool_name,
                           const char *display_summary,
                           int result) {
    if (g_json_output_mode) return;
    if (subagent_id == NULL || tool_name == NULL) return;

    /* Abbreviate ID to first 4 chars */
    char short_id[5] = {0};
    strncpy(short_id, subagent_id, 4);
    short_id[4] = '\0';  /* Explicit null termination for safety */

    /* Map result to display text using local array */
    const char *result_text = (result >= 0 && result < APPROVAL_RESULT_COUNT)
                              ? APPROVAL_RESULT_NAMES[result]
                              : "unknown";

    /* Format detail (truncate if long) */
    char detail[60] = {0};
    if (display_summary != NULL && strlen(display_summary) > 0) {
        if (strlen(display_summary) <= 50) {
            snprintf(detail, sizeof(detail), " (%s)", display_summary);
        } else {
            snprintf(detail, sizeof(detail), " (%.47s...)", display_summary);
        }
    }

    printf(TERM_DIM TERM_GRAY "  ↳ [%s] %s%s → %s" TERM_RESET "\n",
           short_id, tool_name, detail, result_text);
    fflush(stdout);
}

void display_cancellation_message(int tools_completed, int tools_total, bool json_mode) {
    char message[128];
    snprintf(message, sizeof(message), "Operation cancelled (%d/%d tools completed)",
             tools_completed, tools_total);

    if (json_mode) {
        json_output_system("cancelled", message);
    } else {
        printf(TERM_YELLOW TERM_SYM_INFO " %s" TERM_RESET "\n\n", message);
        fflush(stdout);
    }
}
