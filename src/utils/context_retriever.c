#include "context_retriever.h"
#include "../db/vector_db_service.h"
#include "../llm/embeddings_service.h"
#include "common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static context_result_t* create_error_result(const char *error_msg) {
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) return NULL;
    
    result->items = NULL;
    result->item_count = 0;
    result->error = safe_strdup(error_msg);
    
    return result;
}


context_result_t* retrieve_relevant_context(const char *user_message, size_t max_results) {
    if (!user_message || max_results == 0 || strlen(user_message) == 0) {
        // Return empty result for empty messages (not an error)
        context_result_t *result = malloc(sizeof(context_result_t));
        if (!result) return create_error_result("Memory allocation failed");
        
        result->items = NULL;
        result->item_count = 0;
        result->error = NULL;
        return result;
    }
    
    // Get vector database
    vector_db_t *vector_db = vector_db_service_get_database();
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
    
    // Check embeddings service
    if (!embeddings_service_is_configured()) {
        return create_error_result("Embeddings not configured");
    }
    
    // Generate embedding for user message
    vector_t *query_vector = embeddings_service_text_to_vector(user_message);
    if (!query_vector) {
        return create_error_result("Failed to generate embedding for query");
    }
    
    // Search vector database
    search_results_t *search_results = vector_db_search(vector_db, "documents", query_vector, max_results);
    
    if (!search_results) {
        embeddings_service_free_vector(query_vector);
        return create_error_result("Vector search failed");
    }
    
    // Create context result
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) {
        vector_db_free_search_results(search_results);
        embeddings_service_free_vector(query_vector);
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
    embeddings_service_free_vector(query_vector);
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