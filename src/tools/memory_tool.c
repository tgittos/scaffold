#include "memory_tool.h"
#include "../db/vector_db_service.h"
#include "../llm/embeddings_service.h"
#include "../utils/common_utils.h"
#include "tool_result_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MEMORY_INDEX_NAME "long_term_memory"

static size_t next_memory_id = 0;

static vector_db_error_t ensure_memory_index(void) {
    size_t dimension = embeddings_service_get_dimension();
    if (dimension == 0) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    index_config_t config = vector_db_service_get_memory_config(dimension);
    vector_db_error_t result = vector_db_service_ensure_index(MEMORY_INDEX_NAME, &config);
    
    // Free allocated metric string from config
    free(config.metric);
    return result;
}

static char* get_current_timestamp(void) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char *timestamp = malloc(64);
    if (timestamp == NULL) return NULL;
    
    strftime(timestamp, 64, "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}

static char* create_memory_metadata(const char *memory_type, const char *source, const char *importance) {
    char *timestamp = get_current_timestamp();
    if (timestamp == NULL) return NULL;
    
    // Create JSON metadata
    char *metadata = malloc(1024);
    if (metadata == NULL) {
        free(timestamp);
        return NULL;
    }
    
    snprintf(metadata, 1024, 
        "{\"timestamp\": \"%s\", \"type\": \"%s\", \"source\": \"%s\", \"importance\": \"%s\", \"memory_id\": %zu}",
        timestamp,
        memory_type ? memory_type : "general",
        source ? source : "conversation",
        importance ? importance : "normal",
        next_memory_id++
    );
    
    free(timestamp);
    return metadata;
}

int execute_remember_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    // Create result builder
    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;
    
    // Extract parameters first to check for missing required params
    char *content = extract_string_param(tool_call->arguments, "content");
    char *memory_type = extract_string_param(tool_call->arguments, "type");
    char *source = extract_string_param(tool_call->arguments, "source");
    char *importance = extract_string_param(tool_call->arguments, "importance");
    
    
    if (content == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: content");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }
    
    // Check embeddings service
    if (!embeddings_service_is_configured()) {
        tool_result_builder_set_error(builder, "Embeddings service not configured. OPENAI_API_KEY environment variable required");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }
    
    // Ensure memory index exists
    vector_db_error_t index_err = ensure_memory_index();
    if (index_err != VECTOR_DB_OK) {
        tool_result_builder_set_error(builder, "Failed to initialize memory index: %s", 
                                    vector_db_error_string(index_err));
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }
    
    // Generate embedding
    vector_t* vector = embeddings_service_text_to_vector(content);
    if (vector == NULL) {
        tool_result_builder_set_error(builder, "Failed to generate embedding for content");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }
    
    // Get vector database
    vector_db_t *db = vector_db_service_get_database();
    if (db == NULL) {
        tool_result_builder_set_error(builder, "Failed to access vector database");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        embeddings_service_free_vector(vector);
        goto cleanup;
    }
    
    // Create metadata
    char *metadata = create_memory_metadata(memory_type, source, importance);
    if (metadata == NULL) {
        tool_result_builder_set_error(builder, "Failed to create metadata");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        embeddings_service_free_vector(vector);
        goto cleanup;
    }
    
    // Store vector
    size_t memory_id = next_memory_id - 1;
    vector_db_error_t err = vector_db_add_vector(db, MEMORY_INDEX_NAME, vector, memory_id);
    
    if (err == VECTOR_DB_OK) {
        tool_result_builder_set_success(builder, 
            "{\"success\": true, \"memory_id\": %zu, \"message\": \"Memory stored successfully\", \"metadata\": %s}",
            memory_id, metadata);
    } else {
        tool_result_builder_set_error(builder, "Failed to store memory: %s", 
                                    vector_db_error_string(err));
    }
    
    ToolResult* temp_result = tool_result_builder_finalize(builder);
    if (temp_result) {
        *result = *temp_result;
        free(temp_result);
    }
    
    // Cleanup
    embeddings_service_free_vector(vector);
    free(metadata);
    
cleanup:
    free(content);
    free(memory_type);
    free(source);
    free(importance);
    
    return 0;
}

int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    // Create result builder
    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;
    
    // Extract parameters first to check for missing required params
    char *query = extract_string_param(tool_call->arguments, "query");
    double k = extract_number_param(tool_call->arguments, "k", 5);
    
    if (query == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: query");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        free(query);
        return 0;
    }
    
    // Check embeddings service
    if (!embeddings_service_is_configured()) {
        tool_result_builder_set_error(builder, "Embeddings service not configured. OPENAI_API_KEY environment variable required");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        free(query);
        return 0;
    }
    
    // Generate query embedding
    vector_t* query_vector = embeddings_service_text_to_vector(query);
    if (query_vector == NULL) {
        tool_result_builder_set_error(builder, "Failed to generate query embedding");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        free(query);
        return 0;
    }
    
    // Get vector database
    vector_db_t *db = vector_db_service_get_database();
    if (db == NULL) {
        tool_result_builder_set_error(builder, "Failed to access vector database");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        embeddings_service_free_vector(query_vector);
        free(query);
        return 0;
    }
    
    // Search for similar vectors
    search_results_t *search_results = vector_db_search(db, MEMORY_INDEX_NAME, query_vector, (size_t)k);
    
    if (search_results == NULL || search_results->count == 0) {
        tool_result_builder_set_success_json(builder, 
            "{\"success\": true, \"memories\": [], \"message\": \"No relevant memories found\"}");
    } else {
        // Build response with memory IDs and distances
        char response[4096] = "{\"success\": true, \"memories\": [";
        char *p = response + strlen(response);
        
        for (size_t i = 0; i < search_results->count; i++) {
            if (i > 0) {
                p += sprintf(p, ", ");
            }
            p += sprintf(p, "{\"memory_id\": %zu, \"similarity\": %.4f}",
                        search_results->results[i].label,
                        1.0 - search_results->results[i].distance); // Convert distance to similarity
        }
        
        strcat(response, "], \"message\": \"Found relevant memories\"}");
        tool_result_builder_set_success_json(builder, response);
    }
    
    ToolResult* temp_result = tool_result_builder_finalize(builder);
    if (temp_result) {
        *result = *temp_result;
        free(temp_result);
    }
    
    // Cleanup
    if (search_results != NULL) {
        vector_db_free_search_results(search_results);
    }
    embeddings_service_free_vector(query_vector);
    free(query);
    
    return 0;
}

int register_memory_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    
    // Allocate space for new tools
    int current_count = registry->function_count;
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (current_count + 2) * sizeof(ToolFunction));
    if (new_functions == NULL) return -1;
    
    registry->functions = new_functions;
    
    // 1. Register remember tool
    ToolParameter *remember_params = malloc(4 * sizeof(ToolParameter));
    if (remember_params == NULL) return -1;
    
    remember_params[0] = (ToolParameter){
        .name = safe_strdup("content"),
        .type = safe_strdup("string"),
        .description = safe_strdup("The content to remember"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 1
    };
    
    remember_params[1] = (ToolParameter){
        .name = safe_strdup("type"),
        .type = safe_strdup("string"),
        .description = safe_strdup("Type of memory (e.g., 'user_preference', 'fact', 'instruction', 'correction')"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    remember_params[2] = (ToolParameter){
        .name = safe_strdup("source"),
        .type = safe_strdup("string"),
        .description = safe_strdup("Source of the memory (e.g., 'conversation', 'web', 'file')"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    remember_params[3] = (ToolParameter){
        .name = safe_strdup("importance"),
        .type = safe_strdup("string"),
        .description = safe_strdup("Importance level: 'low', 'normal', 'high', 'critical'"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    registry->functions[current_count] = (ToolFunction){
        .name = safe_strdup("remember"),
        .description = safe_strdup("Store important information in long-term memory for future reference"),
        .parameters = remember_params,
        .parameter_count = 4
    };
    
    // 2. Register recall_memories tool
    ToolParameter *recall_params = malloc(2 * sizeof(ToolParameter));
    if (recall_params == NULL) return -1;
    
    recall_params[0] = (ToolParameter){
        .name = safe_strdup("query"),
        .type = safe_strdup("string"),
        .description = safe_strdup("Query to search for relevant memories"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 1
    };
    
    recall_params[1] = (ToolParameter){
        .name = safe_strdup("k"),
        .type = safe_strdup("number"),
        .description = safe_strdup("Number of memories to retrieve (default: 5)"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    registry->functions[current_count + 1] = (ToolFunction){
        .name = safe_strdup("recall_memories"),
        .description = safe_strdup("Search and retrieve relevant memories based on a query"),
        .parameters = recall_params,
        .parameter_count = 2
    };
    
    registry->function_count = current_count + 2;
    
    return 0;
}