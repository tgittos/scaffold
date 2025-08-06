#include "pdf_tool.h"
#include "json_escape.h"
#include "../pdf/pdf_extractor.h"
#include <cJSON.h>
#include "../utils/document_chunker.h"
#include "../llm/embeddings_service.h"
#include "../db/vector_db_service.h"
#include "../utils/common_utils.h"
#include "vector_db_tool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>



static embeddings_config_t* get_embeddings_config(void) {
    static embeddings_config_t config = {0};
    static int initialized = 0;
    
    if (!initialized) {
        // Initialize with environment variables or defaults
        const char *api_key = getenv("OPENAI_API_KEY");
        const char *model = getenv("EMBEDDING_MODEL");
        const char *api_url = getenv("OPENAI_API_URL");
        
        if (!model) model = "text-embedding-3-small";
        
        if (embeddings_init(&config, model, api_key, api_url) == 0) {
            initialized = 1;
        }
    }
    
    return initialized ? &config : NULL;
}

static void auto_process_pdf_for_vector_storage(const char *file_path, const char *extracted_text) {
    if (!file_path || !extracted_text || strlen(extracted_text) == 0) {
        return; // Skip if no content
    }
    
    // Get vector database
    vector_db_t *vector_db = vector_db_service_get_database();
    if (!vector_db) {
        return; // Skip if no vector DB
    }
    
    // Ensure we have a "documents" index for PDFs
    if (!vector_db_has_index(vector_db, "documents")) {
        index_config_t index_config = {
            .dimension = 1536,  // text-embedding-3-small dimension
            .max_elements = 10000,
            .M = 16,
            .ef_construction = 200,
            .random_seed = 100,
            .metric = "cosine"
        };
        
        // Create index if it doesn't exist (ignore error if it already exists)
        vector_db_create_index(vector_db, "documents", &index_config);
    }
    
    // Chunk the document
    chunking_config_t chunk_config = chunker_get_pdf_config();
    chunking_result_t *chunks = chunk_document(extracted_text, &chunk_config);
    
    if (!chunks || chunks->error || chunks->chunk_count == 0) {
        free_chunking_result(chunks);
        return; // Skip if chunking failed
    }
    
    // Get embeddings configuration
    embeddings_config_t *embed_config = get_embeddings_config();
    if (!embed_config) {
        free_chunking_result(chunks);
        return; // Skip if embeddings not configured
    }
    
    // Process each chunk
    size_t vectors_stored = 0;
    for (size_t i = 0; i < chunks->chunk_count; i++) {
        document_chunk_t *chunk = &chunks->chunks[i];
        
        // Generate embedding for chunk
        embedding_vector_t embedding = {0};
        if (embeddings_get_vector(embed_config, chunk->text, &embedding) != 0) {
            continue; // Skip this chunk if embedding fails
        }
        
        // Create vector for storage
        vector_t vector = {
            .data = embedding.data,
            .dimension = embedding.dimension
        };
        
        // Generate unique label for this chunk
        // Use hash of file path + chunk index as label
        size_t label = 0;
        const char *path = file_path;
        for (const char *p = path; *p; p++) {
            label = label * 31 + *p;
        }
        label = label * 1000 + i; // Add chunk index
        
        // Store vector in database
        vector_db_error_t db_error = vector_db_add_vector(vector_db, "documents", &vector, label);
        if (db_error == VECTOR_DB_OK) {
            vectors_stored++;
        }
        
        // Don't free embedding.data - it's now owned by the vector
        embedding.data = NULL;
        embeddings_free_vector(&embedding);
    }
    
    // Cleanup
    free_chunking_result(chunks);
    
    // Note: We don't report the vector storage results to avoid cluttering the main response
    // The AI will get the extracted text as before, but the content is now also searchable
}

int execute_pdf_extract_text_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (!tool_call || !result) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    result->success = 0;
    result->result = NULL;
    
    if (!result->tool_call_id) return -1;
    
    // Extract parameters from tool call arguments
    char *file_path = extract_string_param(tool_call->arguments, "file_path");
    int start_page = (int)extract_number_param(tool_call->arguments, "start_page", -1);
    int end_page = (int)extract_number_param(tool_call->arguments, "end_page", -1);
    int preserve_layout = (int)extract_number_param(tool_call->arguments, "preserve_layout", 1);
    
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
        // Success - automatically process for vector storage
        if (extraction_result->text && extraction_result->length > 0) {
            auto_process_pdf_for_vector_storage(file_path, extraction_result->text);
        }
        
        // Build JSON response with extracted text
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
    if (registry == NULL) {
        return -1;
    }
    
    // Define parameters
    ToolParameter parameters[4];
    
    // Parameter 1: file_path (required)
    parameters[0].name = strdup("file_path");
    parameters[0].type = strdup("string");
    parameters[0].description = strdup("Path to the PDF file to extract text from");
    parameters[0].enum_values = NULL;
    parameters[0].enum_count = 0;
    parameters[0].required = 1;
    
    // Parameter 2: start_page (optional)
    parameters[1].name = strdup("start_page");
    parameters[1].type = strdup("number");
    parameters[1].description = strdup("First page to extract (0-based, -1 for all pages)");
    parameters[1].enum_values = NULL;
    parameters[1].enum_count = 0;
    parameters[1].required = 0;
    
    // Parameter 3: end_page (optional)
    parameters[2].name = strdup("end_page");
    parameters[2].type = strdup("number");
    parameters[2].description = strdup("Last page to extract (0-based, -1 for all pages)");
    parameters[2].enum_values = NULL;
    parameters[2].enum_count = 0;
    parameters[2].required = 0;
    
    // Parameter 4: preserve_layout (optional)
    parameters[3].name = strdup("preserve_layout");
    parameters[3].type = strdup("number");
    parameters[3].description = strdup("Whether to preserve layout formatting (1 for yes, 0 for no)");
    parameters[3].enum_values = NULL;
    parameters[3].enum_count = 0;
    parameters[3].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 4; i++) {
        if (parameters[i].name == NULL || 
            parameters[i].type == NULL ||
            parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(parameters[j].name);
                free(parameters[j].type);
                free(parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    int result = register_tool(registry, "pdf_extract_text", 
                              "Extract text content from a PDF file",
                              parameters, 4, execute_pdf_extract_text_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 4; i++) {
        free(parameters[i].name);
        free(parameters[i].type);
        free(parameters[i].description);
    }
    
    return result;
}