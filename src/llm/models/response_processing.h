#ifndef RESPONSE_PROCESSING_H
#define RESPONSE_PROCESSING_H

#include "output_formatter.h"

// Named constants for thinking tags
#define THINK_START_TAG "<think>"
#define THINK_END_TAG "</think>"
#define THINK_START_TAG_LEN 7
#define THINK_END_TAG_LEN 8

/**
 * Process a response that may contain thinking tags (<think>...</think>).
 *
 * Extracts thinking content (if present) and response content into the
 * ParsedResponse structure. Handles memory allocation for both fields.
 *
 * On success:
 * - If thinking tags present: thinking_content is populated, response_content
 *   contains text after </think> (may be NULL if only whitespace follows)
 * - If no thinking tags: thinking_content is NULL, response_content has full text
 *
 * Caller owns all allocated memory and must free both fields when done.
 *
 * @param content The raw response content from the model
 * @param result Pointer to ParsedResponse structure to populate
 * @return 0 on success, -1 on failure (memory allocation failed)
 */
int process_thinking_response(const char* content, ParsedResponse* result);

/**
 * Process a simple response without thinking tags.
 *
 * Copies the entire content as the response content. Used by models that
 * don't support thinking tags (Claude, GPT, default models).
 *
 * On success: thinking_content is NULL, response_content has full text.
 * Caller owns all allocated memory and must free result->response_content.
 *
 * @param content The raw response content from the model
 * @param result Pointer to ParsedResponse structure to populate
 * @return 0 on success, -1 on failure (memory allocation failed)
 */
int process_simple_response(const char* content, ParsedResponse* result);

#endif // RESPONSE_PROCESSING_H
