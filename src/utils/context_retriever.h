#ifndef CONTEXT_RETRIEVER_H
#define CONTEXT_RETRIEVER_H

#include <stddef.h>
#include "util/darray.h"

typedef struct {
    char *content;
    double relevance_score;
    char *source;
} context_item_t;

DARRAY_DECLARE(ContextItemArray, context_item_t)

typedef struct {
    ContextItemArray items;
    char *error;
} context_result_t;

/**
 * Retrieve relevant context for a user message from vector database
 * 
 * @param user_message The user's message/query
 * @param max_results Maximum number of context items to retrieve
 * @return Context result (caller must free)
 */
context_result_t* retrieve_relevant_context(const char *user_message, size_t max_results);

/**
 * Format context items into a string suitable for inclusion in system prompt
 * 
 * @param context_result Context items to format
 * @return Formatted context string (caller must free)
 */
char* format_context_for_prompt(const context_result_t *context_result);

/**
 * Free context result
 * 
 * @param result Result to free
 */
void free_context_result(context_result_t *result);

#endif // CONTEXT_RETRIEVER_H