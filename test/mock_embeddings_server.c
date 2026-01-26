/**
 * Mock Embeddings Server Helper Implementation
 */

#include "mock_embeddings_server.h"
#include "mock_embeddings.h"
#include <cJSON.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char* mock_embeddings_server_callback(const char* request_body, void* user_data) {
    (void)user_data;  // Unused

    if (request_body == NULL) {
        return NULL;
    }

    // Parse request JSON to get input text
    cJSON* root = cJSON_Parse(request_body);
    if (root == NULL) {
        fprintf(stderr, "mock_embeddings_server: Failed to parse request JSON\n");
        return NULL;
    }

    cJSON* input = cJSON_GetObjectItem(root, "input");
    if (input == NULL || !cJSON_IsString(input)) {
        fprintf(stderr, "mock_embeddings_server: No 'input' field in request\n");
        cJSON_Delete(root);
        return NULL;
    }

    const char* text = input->valuestring;

    // Generate mock embedding
    float embedding[MOCK_EMBEDDING_DIM];
    if (mock_embeddings_get_vector(text, embedding) != 0) {
        fprintf(stderr, "mock_embeddings_server: Failed to generate embedding\n");
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_Delete(root);

    // Build response JSON
    // Allocate buffer large enough for all floats
    size_t buffer_size = MOCK_EMBEDDING_DIM * 20 + 500;  // ~20 chars per float + JSON overhead
    char* response = malloc(buffer_size);
    if (response == NULL) {
        return NULL;
    }

    char* ptr = response;
    size_t remaining = buffer_size;
    int written;

    written = snprintf(ptr, remaining,
        "{"
        "\"object\":\"list\","
        "\"data\":["
        "{"
        "\"object\":\"embedding\","
        "\"index\":0,"
        "\"embedding\":[");
    ptr += written;
    remaining -= written;

    // Write embedding values
    for (size_t i = 0; i < MOCK_EMBEDDING_DIM && remaining > 30; i++) {
        if (i > 0) {
            written = snprintf(ptr, remaining, ",%.8f", embedding[i]);
        } else {
            written = snprintf(ptr, remaining, "%.8f", embedding[i]);
        }
        ptr += written;
        remaining -= written;
    }

    snprintf(ptr, remaining,
        "]"
        "}"
        "],"
        "\"model\":\"text-embedding-3-small\","
        "\"usage\":{\"prompt_tokens\":5,\"total_tokens\":5}"
        "}");

    return response;
}

MockAPIResponse mock_embeddings_server_response(void) {
    return mock_openai_embeddings_dynamic(mock_embeddings_server_callback, NULL);
}
