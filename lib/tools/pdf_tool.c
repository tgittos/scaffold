#include "pdf_tool.h"
#include "../util/json_escape.h"
#include "../pdf/pdf_extractor.h"
#include <cJSON.h>
#include "../util/document_chunker.h"
#include "db/vector_db_service.h"
#include "db/document_store.h"
#include "services/services.h"
#include "../util/common_utils.h"
#include "vector_db_tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PDF_DOCUMENTS_INDEX "documents"
#define PDF_EMBEDDING_DIM 1536

static Services* g_services = NULL;

static void auto_process_pdf_for_vector_storage(const char *file_path, const char *extracted_text) {
    if (!file_path || !extracted_text || strlen(extracted_text) == 0) {
        return;
    }

    document_store_t* doc_store = services_get_document_store(g_services);
    if (doc_store == NULL) {
        return;
    }

    if (document_store_ensure_index(doc_store, PDF_DOCUMENTS_INDEX, PDF_EMBEDDING_DIM, 10000) != 0) {
        return;
    }

    chunking_config_t chunk_config = chunker_get_pdf_config();
    chunking_result_t *chunks = chunk_document(extracted_text, &chunk_config);

    if (!chunks || chunks->error || chunks->chunks.count == 0) {
        free_chunking_result(chunks);
        return;
    }

    char metadata_json[512];
    snprintf(metadata_json, sizeof(metadata_json),
             "{\"source\": \"pdf\", \"file\": \"%s\"}", file_path);

    for (size_t i = 0; i < chunks->chunks.count; i++) {
        document_chunk_t *chunk = &chunks->chunks.data[i];
        vector_db_service_add_text(g_services, PDF_DOCUMENTS_INDEX,
                                   chunk->text, "pdf_chunk", "pdf", metadata_json);
    }

    free_chunking_result(chunks);
}

int execute_pdf_extract_text_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (!tool_call || !result) return -1;

    result->tool_call_id = safe_strdup(tool_call->id);
    result->success = 0;
    result->result = NULL;

    if (!result->tool_call_id) return -1;

    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    int start_page = (int)extract_number_param(tool_call->arguments, "start_page", -1);
    int end_page = (int)extract_number_param(tool_call->arguments, "end_page", -1);

    if (!file_path) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameter: file_path\"}");
        return 0;
    }

    if (pdf_extractor_init() != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize PDF extractor\"}");
        free(file_path);
        return 0;
    }

    pdf_extraction_config_t config = pdf_get_default_config();
    config.start_page = start_page;
    config.end_page = end_page;

    pdf_extraction_result_t *extraction_result = pdf_extract_text_with_config(file_path, &config);

    if (!extraction_result) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"PDF extraction failed - out of memory\"}");
        free(file_path);
        return 0;
    }

    if (extraction_result->error) {
        char error_response[1024];
        snprintf(error_response, sizeof(error_response),
                "{\"success\": false, \"error\": \"PDF extraction failed: %s\"}",
                extraction_result->error);
        result->result = safe_strdup(error_response);
        result->success = 0;
    } else {
        if (extraction_result->text && extraction_result->length > 0) {
            auto_process_pdf_for_vector_storage(file_path, extraction_result->text);
        }

        size_t response_size = extraction_result->length + 512;
        char *response = malloc(response_size);
        if (response) {
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

    pdf_free_extraction_result(extraction_result);
    free(file_path);

    return 0;
}

int register_pdf_tool(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }
    g_services = registry->services;

    ToolParameter parameters[3];
    memset(parameters, 0, sizeof(parameters));

    parameters[0].name = strdup("file_path");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("Path to the PDF file to extract text from");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;

    parameters[1].name = strdup("start_page");
    parameters[1].type = strdup("number");
    parameters[1].description = strdup("First page to extract (0-based, -1 for all pages)");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;

    parameters[2].name = strdup("end_page");
    parameters[2].type = strdup("number");
    parameters[2].description = strdup("Last page to extract (0-based, -1 for all pages)");
    parameters[2].enum_values = NULL;
    parameters[2].enum_count = 0;
    parameters[2].required = 0;

    for (int i = 0; i < 3; i++) {
        if (parameters[i].name == NULL ||
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }

    int result = register_tool(registry, "pdf_extract_text",
                              "Extract text content from a PDF file",
                              parameters, 3, execute_pdf_extract_text_tool_call);

    for (int i = 0; i < 3; i++) {
        free(parameters[i].name);
        free(parameters[i].type);
        free(parameters[i].description);
    }

    return result;
}
