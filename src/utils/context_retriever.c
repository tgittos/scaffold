#include "context_retriever.h"
#include "db/vector_db_service.h"
#include "db/metadata_store.h"
#include "llm/embeddings_service.h"
#include "util/common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DARRAY_DEFINE(ContextItemArray, context_item_t)


static context_result_t* create_error_result(const char *error_msg) {
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) return NULL;

    ContextItemArray_init(&result->items);
    result->error = safe_strdup(error_msg);

    return result;
}


static context_result_t* create_empty_result(void) {
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) return create_error_result("Memory allocation failed");

    ContextItemArray_init(&result->items);
    result->error = NULL;
    return result;
}

context_result_t* retrieve_relevant_context(const char *user_message, size_t max_results) {
    if (!user_message || max_results == 0 || strlen(user_message) == 0) {
        return create_empty_result();
    }

    vector_db_t *vector_db = vector_db_service_get_database();
    if (!vector_db) {
        return create_error_result("Vector database not available");
    }

    if (!vector_db_has_index(vector_db, "documents")) {
        return create_empty_result();
    }
    
    if (!embeddings_service_is_configured()) {
        return create_error_result("Embeddings not configured");
    }
    
    vector_t *query_vector = embeddings_service_text_to_vector(user_message);
    if (!query_vector) {
        return create_error_result("Failed to generate embedding for query");
    }
    
    search_results_t *search_results = vector_db_search(vector_db, "documents", query_vector, max_results);
    
    if (!search_results) {
        embeddings_service_free_vector(query_vector);
        return create_error_result("Vector search failed");
    }
    
    context_result_t *result = malloc(sizeof(context_result_t));
    if (!result) {
        vector_db_free_search_results(search_results);
        embeddings_service_free_vector(query_vector);
        return create_error_result("Memory allocation failed");
    }

    ContextItemArray_init_capacity(&result->items, search_results->count);
    result->error = NULL;

    if (search_results->count == 0) {
        vector_db_free_search_results(search_results);
        embeddings_service_free_vector(query_vector);
        return result;
    }

    // Vector DB stores embeddings only; actual text lives in the metadata store
    metadata_store_t *meta_store = metadata_store_get_instance();

    for (size_t i = 0; i < search_results->count; i++) {
        search_result_t *search_item = &search_results->results[i];

        ChunkMetadata *chunk_meta = NULL;
        if (meta_store) {
            chunk_meta = metadata_store_get(meta_store, "documents", search_item->label);
        }

        if (!chunk_meta || !chunk_meta->content || strlen(chunk_meta->content) == 0) {
            if (chunk_meta) {
                metadata_store_free_chunk(chunk_meta);
            }
            continue;
        }

        context_item_t item;
        item.content = safe_strdup(chunk_meta->content);
        item.relevance_score = 1.0 - search_item->distance;
        item.source = safe_strdup(chunk_meta->source ? chunk_meta->source : "Vector database");

        metadata_store_free_chunk(chunk_meta);

        if (!item.content) {
            free(item.source);
            continue;
        }

        if (ContextItemArray_push(&result->items, item) != 0) {
            free(item.content);
            free(item.source);
            for (size_t j = 0; j < result->items.count; j++) {
                free(result->items.data[j].content);
                free(result->items.data[j].source);
            }
            ContextItemArray_destroy(&result->items);
            vector_db_free_search_results(search_results);
            embeddings_service_free_vector(query_vector);
            free(result);
            return create_error_result("Memory allocation failed");
        }
    }
    
    vector_db_free_search_results(search_results);
    embeddings_service_free_vector(query_vector);
    return result;
}

char* format_context_for_prompt(const context_result_t *context_result) {
    if (!context_result || context_result->items.count == 0) {
        return NULL;
    }

    static const char *header ="\n\n## Relevant Context\n\nThe following information may be relevant to your response:\n\n";
    static const char *footer = "\nPlease use this context to inform your response when relevant.\n";

    size_t total_size = strlen(header) + strlen(footer) + 1;
    for (size_t i = 0; i < context_result->items.count; i++) {
        if (context_result->items.data[i].content) {
            total_size += strlen(context_result->items.data[i].content) + 32;
        }
    }

    char *formatted = malloc(total_size);
    if (!formatted) return NULL;

    strcpy(formatted, header);
    size_t current_len = strlen(formatted);

    for (size_t i = 0; i < context_result->items.count; i++) {
        context_item_t *item = &context_result->items.data[i];
        if (item->content) {
            int written = snprintf(formatted + current_len, total_size - current_len,
                                   "- %s (relevance: %.2f)\n",
                                   item->content, item->relevance_score);
            if (written > 0) {
                current_len += (size_t)written;
            }
        }
    }

    if (current_len + strlen(footer) < total_size) {
        strcpy(formatted + current_len, footer);
    }

    return formatted;
}

void free_context_result(context_result_t *result) {
    if (!result) return;

    for (size_t i = 0; i < result->items.count; i++) {
        free(result->items.data[i].content);
        free(result->items.data[i].source);
    }
    ContextItemArray_destroy(&result->items);

    free(result->error);
    free(result);
}
