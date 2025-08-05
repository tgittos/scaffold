#include "pdf_tool.h"
#include "../pdf/pdf_extractor.h"
#include "../utils/json_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

static char* extract_string_param(const char *json, const char *param_name) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start != '"') return NULL;
    start++; // Skip opening quote
    
    const char *end = start;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && *(end + 1) != '\0') {
            end += 2; // Skip escaped character
        } else {
            end++;
        }
    }
    
    if (*end != '"') return NULL;
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) return NULL;
    
    memcpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

static int extract_number_param(const char *json, const char *param_name, int default_value) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return default_value;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    char *end;
    long value = strtol(start, &end, 10);
    if (end == start) return default_value;
    
    return (int)value;
}

int execute_pdf_extract_text_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (!tool_call || !result) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    result->success = 0;
    result->result = NULL;
    
    if (!result->tool_call_id) return -1;
    
    // Extract parameters from tool call arguments
    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    int start_page = extract_number_param(tool_call->arguments, "start_page", -1);
    int end_page = extract_number_param(tool_call->arguments, "end_page", -1);
    int preserve_layout = extract_number_param(tool_call->arguments, "preserve_layout", 1);
    
    if (!file_path) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameter: file_path\"}");
        return 0;
    }
    
    // Initialize PDF extractor if not already done
    if (pdf_extractor_init() != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize PDF extractor\"}");
        free(file_path);
        return 0;
    }
    
    // Configure extraction
    pdf_extraction_config_t config = pdf_get_default_config();
    config.start_page = start_page;
    config.end_page = end_page;
    config.preserve_layout = preserve_layout;
    
    // Extract text from PDF
    pdf_extraction_result_t *extraction_result = pdf_extract_text_with_config(file_path, &config);
    
    if (!extraction_result) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"PDF extraction failed - out of memory\"}");
        free(file_path);
        return 0;
    }
    
    if (extraction_result->error) {
        // Error occurred during extraction
        char error_response[1024];
        snprintf(error_response, sizeof(error_response), 
                "{\"success\": false, \"error\": \"PDF extraction failed: %s\"}", 
                extraction_result->error);
        result->result = safe_strdup(error_response);
        result->success = 0;
    } else {
        // Success - build JSON response with extracted text
        size_t response_size = extraction_result->length + 512; // Extra space for JSON structure
        char *response = malloc(response_size);
        if (response) {
            // Escape the extracted text for JSON
            char *escaped_text = json_escape_string(extraction_result->text ? extraction_result->text : "");
            if (escaped_text) {
                snprintf(response, response_size,
                        "{\"success\": true, \"text\": \"%s\", \"page_count\": %d, \"length\": %zu}",
                        escaped_text, extraction_result->page_count, extraction_result->length);
                free(escaped_text);
            } else {
                snprintf(response, response_size,
                        "{\"success\": true, \"text\": \"\", \"page_count\": %d, \"length\": 0, \"note\": \"Text escaping failed\"}",
                        extraction_result->page_count);
            }
            result->result = response;
            result->success = 1;
        } else {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to allocate response memory\"}");
            result->success = 0;
        }
    }
    
    // Cleanup
    pdf_free_extraction_result(extraction_result);
    free(file_path);
    
    return 0;
}

int register_pdf_tool(ToolRegistry *registry) {
    if (!registry) return -1;
    
    // Reallocate functions array
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 1) * sizeof(ToolFunction));
    if (!new_functions) return -1;
    
    registry->functions = new_functions;
    ToolFunction *func = &registry->functions[registry->function_count];
    
    // Set function details
    func->name = safe_strdup("pdf_extract_text");
    func->description = safe_strdup("Extract text content from a PDF file");
    func->parameter_count = 4;
    
    // Allocate parameters
    func->parameters = malloc(4 * sizeof(ToolParameter));
    if (!func->parameters) {
        free(func->name);
        free(func->description);
        return -1;
    }
    
    // file_path parameter (required)
    func->parameters[0].name = safe_strdup("file_path");
    func->parameters[0].type = safe_strdup("string");
    func->parameters[0].description = safe_strdup("Path to the PDF file to extract text from");
    func->parameters[0].required = 1;
    func->parameters[0].enum_values = NULL;
    func->parameters[0].enum_count = 0;
    
    // start_page parameter (optional)
    func->parameters[1].name = safe_strdup("start_page");
    func->parameters[1].type = safe_strdup("number");
    func->parameters[1].description = safe_strdup("First page to extract (0-based, -1 for all pages)");
    func->parameters[1].required = 0;
    func->parameters[1].enum_values = NULL;
    func->parameters[1].enum_count = 0;
    
    // end_page parameter (optional)
    func->parameters[2].name = safe_strdup("end_page");
    func->parameters[2].type = safe_strdup("number");
    func->parameters[2].description = safe_strdup("Last page to extract (0-based, -1 for all pages)");
    func->parameters[2].required = 0;
    func->parameters[2].enum_values = NULL;
    func->parameters[2].enum_count = 0;
    
    // preserve_layout parameter (optional)
    func->parameters[3].name = safe_strdup("preserve_layout");
    func->parameters[3].type = safe_strdup("number");
    func->parameters[3].description = safe_strdup("Whether to preserve layout formatting (1 for yes, 0 for no)");
    func->parameters[3].required = 0;
    func->parameters[3].enum_values = NULL;
    func->parameters[3].enum_count = 0;
    
    registry->function_count++;
    
    return 0;
}