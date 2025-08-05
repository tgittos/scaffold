#include "memory_tool.h"
#include "../db/vector_db.h"
#include "../utils/json_utils.h"
#include "../llm/embeddings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MEMORY_INDEX_NAME "long_term_memory"
#define MEMORY_DIMENSION 1536  // OpenAI text-embedding-3-small dimension

static vector_db_t *global_vector_db = NULL;
static size_t next_memory_id = 0;

static vector_db_t* get_or_create_vector_db(void) {
    if (global_vector_db == NULL) {
        global_vector_db = vector_db_create();
        
        // Create memory index if it doesn't exist
        if (!vector_db_has_index(global_vector_db, MEMORY_INDEX_NAME)) {
            index_config_t config = {
                .dimension = MEMORY_DIMENSION,
                .max_elements = 100000,
                .M = 16,
                .ef_construction = 200,
                .random_seed = 42,
                .metric = "cosine"
            };
            vector_db_create_index(global_vector_db, MEMORY_INDEX_NAME, &config);
        }
    }
    return global_vector_db;
}

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
    
    // Copy and handle escape sequences
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            switch (start[i + 1]) {
                case 'n': result[j++] = '\n'; i++; break;
                case 't': result[j++] = '\t'; i++; break;
                case 'r': result[j++] = '\r'; i++; break;
                case '"': result[j++] = '"'; i++; break;
                case '\\': result[j++] = '\\'; i++; break;
                default: result[j++] = start[i]; break;
            }
        } else {
            result[j++] = start[i];
        }
    }
    result[j] = '\0';
    
    return result;
}

static double extract_number_param(const char *json, const char *param_name, double default_value) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return default_value;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    char *end;
    double value = strtod(start, &end);
    if (end == start) return default_value;
    
    return value;
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
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *content = extract_string_param(tool_call->arguments, "content");
    char *memory_type = extract_string_param(tool_call->arguments, "type");
    char *source = extract_string_param(tool_call->arguments, "source");
    char *importance = extract_string_param(tool_call->arguments, "importance");
    
    if (content == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameter: content\"}");
        result->success = 0;
        free(memory_type);
        free(source);
        free(importance);
        return 0;
    }
    
    // Get API key from environment
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"OPENAI_API_KEY environment variable not set\"}");
        result->success = 0;
        free(content);
        free(memory_type);
        free(source);
        free(importance);
        return 0;
    }
    
    // Initialize embeddings
    embeddings_config_t embed_config;
    if (embeddings_init(&embed_config, "text-embedding-3-small", api_key, NULL) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize embeddings\"}");
        result->success = 0;
        free(content);
        free(memory_type);
        free(source);
        free(importance);
        return 0;
    }
    
    // Get embedding for content
    embedding_vector_t embedding;
    if (embeddings_get_vector(&embed_config, content, &embedding) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to generate embedding\"}");
        result->success = 0;
        embeddings_cleanup(&embed_config);
        free(content);
        free(memory_type);
        free(source);
        free(importance);
        return 0;
    }
    
    // Get or create vector DB
    vector_db_t *db = get_or_create_vector_db();
    if (db == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to access vector database\"}");
        result->success = 0;
        embeddings_free_vector(&embedding);
        embeddings_cleanup(&embed_config);
        free(content);
        free(memory_type);
        free(source);
        free(importance);
        return 0;
    }
    
    // Create metadata
    char *metadata = create_memory_metadata(memory_type, source, importance);
    if (metadata == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to create metadata\"}");
        result->success = 0;
        embeddings_free_vector(&embedding);
        embeddings_cleanup(&embed_config);
        free(content);
        free(memory_type);
        free(source);
        free(importance);
        return 0;
    }
    
    // Convert embedding to vector_t
    vector_t vec = {
        .data = embedding.data,
        .dimension = embedding.dimension
    };
    
    // Add vector with metadata stored at the memory_id
    vector_db_error_t err = vector_db_add_vector(db, MEMORY_INDEX_NAME, &vec, next_memory_id - 1);
    
    if (err == VECTOR_DB_OK) {
        // Store the actual content and metadata separately (in a real system, this would be in a key-value store)
        // For now, we'll include it in the success response
        char response[2048];
        snprintf(response, sizeof(response),
            "{\"success\": true, \"memory_id\": %zu, \"message\": \"Memory stored successfully\", \"metadata\": %s}",
            next_memory_id - 1, metadata);
        result->result = safe_strdup(response);
        result->success = 1;
    } else {
        char error_msg[512];
        snprintf(error_msg, sizeof(error_msg),
            "{\"success\": false, \"error\": \"Failed to store memory: %s\"}",
            vector_db_error_string(err));
        result->result = safe_strdup(error_msg);
        result->success = 0;
    }
    
    // Cleanup
    embeddings_cleanup(&embed_config);
    free(content);
    free(memory_type);
    free(source);
    free(importance);
    free(metadata);
    
    return 0;
}

int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *query = extract_string_param(tool_call->arguments, "query");
    double k = extract_number_param(tool_call->arguments, "k", 5);
    
    if (query == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameter: query\"}");
        result->success = 0;
        return 0;
    }
    
    // Get API key from environment
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"OPENAI_API_KEY environment variable not set\"}");
        result->success = 0;
        free(query);
        return 0;
    }
    
    // Initialize embeddings
    embeddings_config_t embed_config;
    if (embeddings_init(&embed_config, "text-embedding-3-small", api_key, NULL) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize embeddings\"}");
        result->success = 0;
        free(query);
        return 0;
    }
    
    // Get embedding for query
    embedding_vector_t embedding;
    if (embeddings_get_vector(&embed_config, query, &embedding) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to generate query embedding\"}");
        result->success = 0;
        embeddings_cleanup(&embed_config);
        free(query);
        return 0;
    }
    
    // Get vector DB
    vector_db_t *db = get_or_create_vector_db();
    if (db == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to access vector database\"}");
        result->success = 0;
        embeddings_free_vector(&embedding);
        embeddings_cleanup(&embed_config);
        free(query);
        return 0;
    }
    
    // Convert embedding to vector_t
    vector_t query_vec = {
        .data = embedding.data,
        .dimension = embedding.dimension
    };
    
    // Search for similar vectors
    search_results_t *search_results = vector_db_search(db, MEMORY_INDEX_NAME, &query_vec, (size_t)k);
    
    if (search_results == NULL || search_results->count == 0) {
        result->result = safe_strdup("{\"success\": true, \"memories\": [], \"message\": \"No relevant memories found\"}");
        result->success = 1;
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
        result->result = safe_strdup(response);
        result->success = 1;
    }
    
    // Cleanup
    if (search_results != NULL) {
        vector_db_free_search_results(search_results);
    }
    embeddings_free_vector(&embedding);
    embeddings_cleanup(&embed_config);
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