/*
 * tool_format.h - Tool Format Strategy Pattern
 *
 * Abstracts provider-specific tool JSON generation and parsing.
 * Each LLM provider (OpenAI, Anthropic, etc.) has its own format.
 */

#ifndef TOOL_FORMAT_H
#define TOOL_FORMAT_H

#include "tools_system.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>

/*
 * Tool format strategy interface.
 * Implementations provide provider-specific JSON generation and parsing.
 */
typedef struct ToolFormatStrategy {
    const char *name;

    /*
     * Generate tools JSON array for API request.
     * Returns: Newly allocated JSON string, or NULL on error.
     */
    char *(*generate_tools_json)(const ToolRegistry *registry);

    /*
     * Parse tool calls from API response.
     * Returns: 0 on success (even if no calls found), -1 on error.
     */
    int (*parse_tool_calls)(const char *response, ToolCall **calls, int *count);

    /*
     * Format a single tool result for the API.
     * Returns: Newly allocated JSON string, or NULL on error.
     */
    char *(*format_tool_result)(const ToolResult *result);
} ToolFormatStrategy;

/*
 * Get the format strategy for a given provider.
 *
 * Parameters:
 *   provider - Provider name ("openai", "anthropic", "local", etc.)
 *
 * Returns: Strategy pointer (never NULL - falls back to OpenAI format).
 */
const ToolFormatStrategy *get_tool_format_strategy(const char *provider);

/*
 * OpenAI format strategy.
 * Uses function calling format with tool_calls array in response.
 */
extern const ToolFormatStrategy tool_format_openai;

/*
 * Anthropic format strategy.
 * Uses input_schema in request and tool_use blocks in response.
 */
extern const ToolFormatStrategy tool_format_anthropic;

/*
 * Common JSON helper functions for format implementations.
 */
static inline char *tool_format_extract_string(const char *json, const char *key) {
    cJSON *json_obj = cJSON_Parse(json);
    if (json_obj == NULL) {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItem(json_obj, key);
    char *result = NULL;
    if (cJSON_IsString(item)) {
        result = strdup(cJSON_GetStringValue(item));
    }

    cJSON_Delete(json_obj);
    return result;
}

static inline char *tool_format_extract_object(const char *json, const char *key) {
    cJSON *json_obj = cJSON_Parse(json);
    if (json_obj == NULL) {
        return NULL;
    }

    cJSON *item = cJSON_GetObjectItem(json_obj, key);
    char *result = NULL;
    if (item != NULL) {
        result = cJSON_PrintUnformatted(item);
    }

    cJSON_Delete(json_obj);
    return result;
}

#endif /* TOOL_FORMAT_H */
