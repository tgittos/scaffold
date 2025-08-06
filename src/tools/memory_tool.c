#include "memory_tool.h"
#include "../db/vector_db_service.h"
#include "../db/metadata_store.h"
#include "../llm/embeddings_service.h"
#include "../utils/common_utils.h"
#include "../utils/json_escape.h"
#include "../utils/debug_output.h"
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
        // Store metadata and content
        metadata_store_t* meta_store = metadata_store_get_instance();
        if (meta_store != NULL) {
            ChunkMetadata chunk = {
                .chunk_id = memory_id,
                .content = content,
                .index_name = (char*)MEMORY_INDEX_NAME,
                .type = memory_type ? memory_type : "general",
                .source = source ? source : "conversation",
                .importance = importance ? importance : "normal",
                .timestamp = time(NULL),
                .custom_metadata = metadata
            };
            
            if (metadata_store_save(meta_store, &chunk) != 0) {
                debug_printf("Warning: Failed to store metadata for memory %zu\n", memory_id);
            }
        }
        
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

int execute_forget_memory_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    // Create result builder
    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;
    
    // Extract memory_id parameter
    double memory_id_num = extract_number_param(tool_call->arguments, "memory_id", -1);
    
    if (memory_id_num < 0) {
        tool_result_builder_set_error(builder, "Missing or invalid required parameter: memory_id");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }
    
    size_t memory_id = (size_t)memory_id_num;
    
    // Get vector database
    vector_db_t *db = vector_db_service_get_database();
    if (db == NULL) {
        tool_result_builder_set_error(builder, "Failed to access vector database");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }
    
    // Get metadata store
    metadata_store_t* meta_store = metadata_store_get_instance();
    if (meta_store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access metadata store");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }
    
    // Try to get the memory metadata first to verify it exists
    ChunkMetadata* chunk = metadata_store_get(meta_store, MEMORY_INDEX_NAME, memory_id);
    if (chunk == NULL) {
        tool_result_builder_set_error(builder, "Memory with ID %zu not found", memory_id);
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }
    
    // Store some info about the deleted memory for the response
    char* memory_type = chunk->type ? strdup(chunk->type) : strdup("unknown");
    char* content_preview = NULL;
    if (chunk->content) {
        size_t len = strlen(chunk->content);
        if (len > 50) {
            content_preview = malloc(54);
            if (content_preview) {
                strncpy(content_preview, chunk->content, 50);
                strcpy(content_preview + 50, "...");
            }
        } else {
            content_preview = strdup(chunk->content);
        }
    }
    metadata_store_free_chunk(chunk);
    
    // Delete from vector database
    vector_db_error_t err = vector_db_delete_vector(db, MEMORY_INDEX_NAME, memory_id);
    bool vector_deleted = (err == VECTOR_DB_OK || err == VECTOR_DB_ERROR_ELEMENT_NOT_FOUND);
    
    // Delete from metadata store
    bool metadata_deleted = (metadata_store_delete(meta_store, MEMORY_INDEX_NAME, memory_id) == 0);
    
    if (vector_deleted && metadata_deleted) {
        char response[1024];
        snprintf(response, sizeof(response),
            "{\"success\": true, \"memory_id\": %zu, \"message\": \"Memory deleted successfully\", \"deleted\": {\"type\": \"%s\", \"preview\": \"%s\"}}",
            memory_id, memory_type, content_preview ? content_preview : "");
        tool_result_builder_set_success_json(builder, response);
    } else if (metadata_deleted) {
        // Metadata deleted but vector might have already been missing
        char response[1024];
        snprintf(response, sizeof(response),
            "{\"success\": true, \"memory_id\": %zu, \"message\": \"Memory metadata deleted (vector was already absent)\", \"deleted\": {\"type\": \"%s\"}}",
            memory_id, memory_type);
        tool_result_builder_set_success_json(builder, response);
    } else {
        tool_result_builder_set_error(builder, "Failed to delete memory with ID %zu", memory_id);
    }
    
    ToolResult* temp_result = tool_result_builder_finalize(builder);
    if (temp_result) {
        *result = *temp_result;
        free(temp_result);
    }
    
    // Cleanup
    free(memory_type);
    free(content_preview);
    
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
        // Build response with memory content
        metadata_store_t* meta_store = metadata_store_get_instance();
        char* response = malloc(65536); // Allocate more space for content
        if (response == NULL) {
            tool_result_builder_set_error(builder, "Memory allocation failed");
            goto recall_cleanup;
        }
        
        strcpy(response, "{\"success\": true, \"memories\": [");
        char *p = response + strlen(response);
        
        for (size_t i = 0; i < search_results->count; i++) {
            if (i > 0) {
                p += sprintf(p, ", ");
            }
            
            size_t memory_id = search_results->results[i].label;
            float similarity = 1.0 - search_results->results[i].distance;
            
            // Try to get content from metadata store
            ChunkMetadata* chunk = NULL;
            if (meta_store != NULL) {
                chunk = metadata_store_get(meta_store, MEMORY_INDEX_NAME, memory_id);
            }
            
            p += sprintf(p, "{\"memory_id\": %zu, \"similarity\": %.4f", memory_id, similarity);
            
            if (chunk != NULL && chunk->content != NULL) {
                // Escape content for JSON
                char* escaped_content = json_escape_string(chunk->content);
                if (escaped_content != NULL) {
                    p += sprintf(p, ", \"content\": \"%s\"", escaped_content);
                    free(escaped_content);
                }
                
                if (chunk->type != NULL) {
                    p += sprintf(p, ", \"type\": \"%s\"", chunk->type);
                }
                
                if (chunk->source != NULL) {
                    p += sprintf(p, ", \"source\": \"%s\"", chunk->source);
                }
                
                metadata_store_free_chunk(chunk);
            }
            
            p += sprintf(p, "}");
        }
        
        strcat(response, "], \"message\": \"Found relevant memories\"}");
        tool_result_builder_set_success_json(builder, response);
        free(response);
    }
    
recall_cleanup:
    
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
    int result;
    
    // 1. Register remember tool
    ToolParameter remember_parameters[4];
    
    remember_parameters[0].name = strdup("content");
    remember_parameters[0].type = strdup("string");
    remember_parameters[0].description = strdup("The content to remember");
    remember_parameters[0].enum_values = NULL;
    remember_parameters[0].enum_count = 0;
    remember_parameters[0].required = 1;
    
    remember_parameters[1].name = strdup("type");
    remember_parameters[1].type = strdup("string");
    remember_parameters[1].description = strdup("Type of memory (e.g., 'user_preference', 'fact', 'instruction', 'correction')");
    remember_parameters[1].enum_values = NULL;
    remember_parameters[1].enum_count = 0;
    remember_parameters[1].required = 0;
    
    remember_parameters[2].name = strdup("source");
    remember_parameters[2].type = strdup("string");
    remember_parameters[2].description = strdup("Source of the memory (e.g., 'conversation', 'web', 'file')");
    remember_parameters[2].enum_values = NULL;
    remember_parameters[2].enum_count = 0;
    remember_parameters[2].required = 0;
    
    remember_parameters[3].name = strdup("importance");
    remember_parameters[3].type = strdup("string");
    remember_parameters[3].description = strdup("Importance level: 'low', 'normal', 'high', 'critical'");
    remember_parameters[3].enum_values = NULL;
    remember_parameters[3].enum_count = 0;
    remember_parameters[3].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 4; i++) {
        if (remember_parameters[i].name == NULL || 
            remember_parameters[i].type == NULL ||
            remember_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(remember_parameters[j].name);
                free(remember_parameters[j].type);
                free(remember_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "remember", 
                          "Store important information in long-term memory for future reference",
                          remember_parameters, 4, execute_remember_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 4; i++) {
        free(remember_parameters[i].name);
        free(remember_parameters[i].type);
        free(remember_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 2. Register recall_memories tool
    ToolParameter recall_parameters[2];
    
    recall_parameters[0].name = strdup("query");
    recall_parameters[0].type = strdup("string");
    recall_parameters[0].description = strdup("Query to search for relevant memories");
    recall_parameters[0].enum_values = NULL;
    recall_parameters[0].enum_count = 0;
    recall_parameters[0].required = 1;
    
    recall_parameters[1].name = strdup("k");
    recall_parameters[1].type = strdup("number");
    recall_parameters[1].description = strdup("Number of memories to retrieve (default: 5)");
    recall_parameters[1].enum_values = NULL;
    recall_parameters[1].enum_count = 0;
    recall_parameters[1].required = 0;
    
    // Check for allocation failures
    for (int i = 0; i < 2; i++) {
        if (recall_parameters[i].name == NULL || 
            recall_parameters[i].type == NULL ||
            recall_parameters[i].description == NULL) {
            // Cleanup on failure
            for (int j = 0; j <= i; j++) {
                free(recall_parameters[j].name);
                free(recall_parameters[j].type);
                free(recall_parameters[j].description);
            }
            return -1;
        }
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "recall_memories", 
                          "Search and retrieve relevant memories based on a query",
                          recall_parameters, 2, execute_recall_memories_tool_call);
    
    // Clean up temporary parameter storage
    for (int i = 0; i < 2; i++) {
        free(recall_parameters[i].name);
        free(recall_parameters[i].type);
        free(recall_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 3. Register forget_memory tool
    ToolParameter forget_parameters[1];
    
    forget_parameters[0].name = strdup("memory_id");
    forget_parameters[0].type = strdup("number");
    forget_parameters[0].description = strdup("The ID of the memory to delete");
    forget_parameters[0].enum_values = NULL;
    forget_parameters[0].enum_count = 0;
    forget_parameters[0].required = 1;
    
    // Check for allocation failures
    if (forget_parameters[0].name == NULL || 
        forget_parameters[0].type == NULL ||
        forget_parameters[0].description == NULL) {
        free(forget_parameters[0].name);
        free(forget_parameters[0].type);
        free(forget_parameters[0].description);
        return -1;
    }
    
    // Register the tool using the new system
    result = register_tool(registry, "forget_memory", 
                          "Delete a specific memory from long-term storage by its ID",
                          forget_parameters, 1, execute_forget_memory_tool_call);
    
    // Clean up temporary parameter storage
    free(forget_parameters[0].name);
    free(forget_parameters[0].type);
    free(forget_parameters[0].description);
    
    return result;
}