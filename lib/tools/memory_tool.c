#include "memory_tool.h"
#include "db/vector_db_service.h"
#include "db/metadata_store.h"
#include "llm/embeddings_service.h"
#include "../util/common_utils.h"
#include "../util/json_escape.h"
#include "../util/debug_output.h"
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
    
    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

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
    
    if (!embeddings_service_is_configured()) {
        tool_result_builder_set_error(builder, "Embeddings service not configured. OPENAI_API_KEY environment variable required");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }
    
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
    
    size_t memory_id = next_memory_id - 1;
    vector_db_error_t err = vector_db_add_vector(db, MEMORY_INDEX_NAME, vector, memory_id);
    
    if (err == VECTOR_DB_OK) {
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
    
    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

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
    
    vector_db_error_t err = vector_db_delete_vector(db, MEMORY_INDEX_NAME, memory_id);
    bool vector_deleted = (err == VECTOR_DB_OK || err == VECTOR_DB_ERROR_ELEMENT_NOT_FOUND);
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
    
    free(memory_type);
    free(content_preview);
    
    return 0;
}

int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char *query = extract_string_param(tool_call->arguments, "query");
    double k = extract_number_param(tool_call->arguments, "k", 5);
    
    if (query == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: query");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }

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

    metadata_store_t* meta_store = metadata_store_get_instance();
    if (meta_store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access metadata store");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        free(query);
        return 0;
    }
    
    size_t text_count = 0;
    ChunkMetadata** text_results = metadata_store_search(meta_store, MEMORY_INDEX_NAME, query, &text_count);

    search_results_t *vector_results = NULL;
    if (embeddings_service_is_configured()) {
        vector_t* query_vector = embeddings_service_text_to_vector(query);
        if (query_vector != NULL) {
            vector_db_t *db = vector_db_service_get_database();
            if (db != NULL) {
                // Fetch 2x results to allow deduplication with text search hits
                vector_results = vector_db_search(db, MEMORY_INDEX_NAME, query_vector, (size_t)(k * 2));
            }
            embeddings_service_free_vector(query_vector);
        }
    }
    
    // Hybrid search: merge text-exact and vector-similarity results with scoring
    typedef struct {
        size_t memory_id;
        float score;
        ChunkMetadata* chunk;
        bool from_text_search;
    } ScoredResult;
    
    size_t max_results = (text_count + (vector_results ? vector_results->count : 0));
    ScoredResult* scored_results = calloc(max_results, sizeof(ScoredResult));
    if (scored_results == NULL) {
        tool_result_builder_set_error(builder, "Memory allocation failed");
        goto recall_cleanup;
    }
    
    size_t result_count = 0;
    
    for (size_t i = 0; i < text_count && i < (size_t)k; i++) {
        scored_results[result_count].memory_id = text_results[i]->chunk_id;
        scored_results[result_count].score = 1.0; // Perfect score for text matches
        scored_results[result_count].chunk = text_results[i];
        scored_results[result_count].from_text_search = true;
        result_count++;
    }
    
    if (vector_results != NULL) {
        for (size_t i = 0; i < vector_results->count; i++) {
            size_t memory_id = vector_results->results[i].label;
            float similarity = 1.0 - vector_results->results[i].distance;
            
            bool is_duplicate = false;
            for (size_t j = 0; j < result_count; j++) {
                if (scored_results[j].memory_id == memory_id) {
                    // Boost: appearing in both text and vector search implies higher relevance
                    scored_results[j].score = (scored_results[j].score + similarity) / 2.0 + 0.1;
                    is_duplicate = true;
                    break;
                }
            }
            
            if (!is_duplicate && result_count < (size_t)k) {
                scored_results[result_count].memory_id = memory_id;
                scored_results[result_count].score = similarity;
                scored_results[result_count].chunk = metadata_store_get(meta_store, MEMORY_INDEX_NAME, memory_id);
                scored_results[result_count].from_text_search = false;
                result_count++;
            }
        }
    }
    
    for (size_t i = 0; result_count > 0 && i < result_count - 1; i++) {
        for (size_t j = i + 1; j < result_count; j++) {
            if (scored_results[j].score > scored_results[i].score) {
                ScoredResult temp = scored_results[i];
                scored_results[i] = scored_results[j];
                scored_results[j] = temp;
            }
        }
    }
    
    if (result_count == 0) {
        tool_result_builder_set_success_json(builder, 
            "{\"success\": true, \"memories\": [], \"message\": \"No relevant memories found\"}");
    } else {
        char* response = malloc(65536); // Allocate more space for content
        if (response == NULL) {
            tool_result_builder_set_error(builder, "Memory allocation failed");
            goto recall_cleanup_scored;
        }
        
        strcpy(response, "{\"success\": true, \"memories\": [");
        char *p = response + strlen(response);
        
        size_t output_count = result_count > (size_t)k ? (size_t)k : result_count;
        for (size_t i = 0; i < output_count; i++) {
            if (i > 0) {
                p += sprintf(p, ", ");
            }
            
            p += sprintf(p, "{\"memory_id\": %zu, \"score\": %.4f, \"match_type\": \"%s\"", 
                         scored_results[i].memory_id, 
                         scored_results[i].score,
                         scored_results[i].from_text_search ? "text" : "semantic");
            
            ChunkMetadata* chunk = scored_results[i].chunk;
            if (chunk != NULL && chunk->content != NULL) {
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
                
                if (chunk->importance != NULL) {
                    p += sprintf(p, ", \"importance\": \"%s\"", chunk->importance);
                }
            }
            
            p += sprintf(p, "}");
        }
        
        strcat(response, "], \"message\": \"Found relevant memories using hybrid search\"}");
        tool_result_builder_set_success_json(builder, response);
        free(response);
    }
    
recall_cleanup_scored:
    for (size_t i = 0; i < result_count; i++) {
        if (!scored_results[i].from_text_search && scored_results[i].chunk != NULL) {
            metadata_store_free_chunk(scored_results[i].chunk);
        }
    }
    free(scored_results);
    
recall_cleanup:
    
    ToolResult* temp_result = tool_result_builder_finalize(builder);
    if (temp_result) {
        *result = *temp_result;
        free(temp_result);
    }
    
    if (text_results != NULL) {
        metadata_store_free_chunks(text_results, text_count);
    }
    if (vector_results != NULL) {
        vector_db_free_search_results(vector_results);
    }
    free(query);
    
    return 0;
}

int register_memory_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    int result;
    
    ToolParameter remember_parameters[4];
    memset(remember_parameters, 0, sizeof(remember_parameters));

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
    
    for (int i = 0; i <4; i++) {
        if (remember_parameters[i].name == NULL || 
            remember_parameters[i].type == NULL ||
            remember_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(remember_parameters[j].name);
                free(remember_parameters[j].type);
                free(remember_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "remember", 
                          "Store important information in long-term memory for future reference",
                          remember_parameters, 4, execute_remember_tool_call);
    
    for (int i = 0; i < 4; i++) {
        free(remember_parameters[i].name);
        free(remember_parameters[i].type);
        free(remember_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    ToolParameter recall_parameters[2];
    memset(recall_parameters, 0, sizeof(recall_parameters));

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
    
    for (int i = 0; i <2; i++) {
        if (recall_parameters[i].name == NULL || 
            recall_parameters[i].type == NULL ||
            recall_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(recall_parameters[j].name);
                free(recall_parameters[j].type);
                free(recall_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "recall_memories", 
                          "Search and retrieve relevant memories based on a query",
                          recall_parameters, 2, execute_recall_memories_tool_call);
    
    for (int i = 0; i < 2; i++) {
        free(recall_parameters[i].name);
        free(recall_parameters[i].type);
        free(recall_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    ToolParameter forget_parameters[1];
    memset(forget_parameters, 0, sizeof(forget_parameters));

    forget_parameters[0].name = strdup("memory_id");
    forget_parameters[0].type = strdup("number");
    forget_parameters[0].description = strdup("The ID of the memory to delete");
    forget_parameters[0].enum_values = NULL;
    forget_parameters[0].enum_count = 0;
    forget_parameters[0].required = 1;
    
    if (forget_parameters[0].name == NULL ||
        forget_parameters[0].type == NULL ||
        forget_parameters[0].description == NULL) {
        free(forget_parameters[0].name);
        free(forget_parameters[0].type);
        free(forget_parameters[0].description);
        return -1;
    }
    
    result = register_tool(registry, "forget_memory", 
                          "Delete a specific memory from long-term storage by its ID",
                          forget_parameters, 1, execute_forget_memory_tool_call);
    
    free(forget_parameters[0].name);
    free(forget_parameters[0].type);
    free(forget_parameters[0].description);
    
    return result;
}