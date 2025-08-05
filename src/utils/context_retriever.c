#include "context_retriever.h"
#include "../tools/vector_db_tool.h"
#include "../llm/embeddings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

static context_result_t* create_error_result(const char *error_msg) {
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) return NULL;
    
    result->items = NULL;
    result->item_count = 0;
    result->error = safe_strdup(error_msg);
    
    return result;
}

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

context_result_t* retrieve_relevant_context(const char *user_message, size_t max_results) {
    if (!user_message || max_results == 0) {
        return create_error_result("Invalid parameters");
    }
    
    // Get vector database
    vector_db_t *vector_db = get_global_vector_db();
    if (!vector_db) {
        return create_error_result("Vector database not available");
    }
    
    // Check if documents index exists
    if (!vector_db_has_index(vector_db, "documents")) {
        // No documents index, return empty result (not an error)
        context_result_t *result = malloc(sizeof(context_result_t));
        if (!result) return create_error_result("Memory allocation failed");
        
        result->items = NULL;
        result->item_count = 0;
        result->error = NULL;
        return result;
    }
    
    // Get embeddings configuration
    embeddings_config_t *embed_config = get_embeddings_config();
    if (!embed_config) {
        return create_error_result("Embeddings not configured");
    }
    
    // Generate embedding for user message
    embedding_vector_t query_embedding = {0};
    if (embeddings_get_vector(embed_config, user_message, &query_embedding) != 0) {
        return create_error_result("Failed to generate embedding for query");
    }
    
    // Create vector for search
    vector_t query_vector = {
        .data = query_embedding.data,
        .dimension = query_embedding.dimension
    };
    
    // Search vector database
    search_results_t *search_results = vector_db_search(vector_db, "documents", &query_vector, max_results);
    
    // Clean up query embedding (data is now owned by query_vector)
    query_embedding.data = NULL;
    embeddings_free_vector(&query_embedding);
    
    if (!search_results) {
        return create_error_result("Vector search failed");
    }
    
    // Create context result
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) {
        vector_db_free_search_results(search_results);
        return create_error_result("Memory allocation failed");
    }
    
    result->item_count = search_results->count;
    result->error = NULL;
    
    if (result->item_count == 0) {
        result->items = NULL;
        vector_db_free_search_results(search_results);
        return result;
    }
    
    // Allocate context items
    result->items = malloc(result->item_count * sizeof(context_item_t));
    if (!result->items) {
        vector_db_free_search_results(search_results);
        free(result);
        return create_error_result("Memory allocation failed");
    }
    
    // Fill context items
    for (size_t i = 0; i < result->item_count; i++) {
        context_item_t *item = &result->items[i];
        search_result_t *search_item = &search_results->results[i];
        
        // For now, we don't have a way to retrieve the original text from labels
        // This would require storing chunk text alongside vectors
        // For MVP, we'll create a placeholder
        item->content = safe_strdup("Relevant document chunk (text retrieval not implemented yet)");
        item->relevance_score = 1.0 - search_item->distance; // Convert distance to relevance
        item->source = safe_strdup("Vector database");
    }
    
    vector_db_free_search_results(search_results);
    return result;
}

char* format_context_for_prompt(const context_result_t *context_result) {
    if (!context_result || context_result->item_count == 0) {
        return NULL;
    }
    
    // Calculate required buffer size
    size_t total_size = 256; // Base overhead
    for (size_t i = 0; i < context_result->item_count; i++) {
        if (context_result->items[i].content) {
            total_size += strlen(context_result->items[i].content) + 100;
        }
    }
    
    char *formatted = malloc(total_size);
    if (!formatted) return NULL;
    
    strcpy(formatted, "\n\n## Relevant Context\n\nThe following information may be relevant to your response:\n\n");
    
    for (size_t i = 0; i < context_result->item_count; i++) {
        context_item_t *item = &context_result->items[i];
        if (item->content) {
            char item_text[512];
            snprintf(item_text, sizeof(item_text), 
                    "- %s (relevance: %.2f)\n", 
                    item->content, item->relevance_score);
            strcat(formatted, item_text);
        }
    }
    
    strcat(formatted, "\nPlease use this context to inform your response when relevant.\n");
    
    return formatted;
}

void free_context_result(context_result_t *result) {
    if (!result) return;
    
    if (result->items) {
        for (size_t i = 0; i < result->item_count; i++) {
            free(result->items[i].content);
            free(result->items[i].source);
        }
        free(result->items);
    }
    
    free(result->error);
    free(result);
}