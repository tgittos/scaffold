#include "../embedding_provider.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int openai_embedding_detect_provider(const char* api_url);
static char* openai_embedding_build_request_json(const EmbeddingProvider* provider,
                                                const char* model,
                                                const char* text);
static int openai_embedding_build_headers(const EmbeddingProvider* provider,
                                         const char* api_key,
                                         const char** headers,
                                         int max_headers);
static int openai_embedding_parse_response(const EmbeddingProvider* provider,
                                          const char* json_response,
                                          embedding_vector_t* embedding);

// OpenAI embedding provider implementation
static int openai_embedding_detect_provider(const char* api_url) {
    if (api_url == NULL) return 0;
    return strstr(api_url, "api.openai.com") != NULL ||
           strstr(api_url, "openai.azure.com") != NULL ||  // Support Azure OpenAI
           strstr(api_url, "api.groq.com") != NULL;        // Support Groq (OpenAI-compatible)
}

static char* openai_embedding_build_request_json(const EmbeddingProvider* provider,
                                                const char* model,
                                                const char* text) {
    (void)provider; // Suppress unused parameter warning

    if (model == NULL || text == NULL || strlen(text) == 0) {
        fprintf(stderr, "Error: model and non-empty text parameters are required for embeddings request\n");
        return NULL;
    }

    cJSON* json = cJSON_CreateObject();
    if (!json) {
        return NULL;
    }

    if (!cJSON_AddStringToObject(json, "model", model) ||
        !cJSON_AddStringToObject(json, "input", text)) {
        cJSON_Delete(json);
        return NULL;
    }

    char* json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    return json_string;
}

static int openai_embedding_build_headers(const EmbeddingProvider* provider,
                                         const char* api_key,
                                         const char** headers,
                                         int max_headers) {
    (void)provider; // Suppress unused parameter warning

    if (max_headers < 1) {
        return 0;
    }

    int count = 0;
    static _Thread_local char auth_header[512];

    // Add authorization header if API key provided and non-empty
    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[count++] = auth_header;
    }

    // Note: Content-Type is handled by http_client automatically

    return count;
}

static int openai_embedding_parse_response(const EmbeddingProvider* provider,
                                          const char* json_response,
                                          embedding_vector_t* embedding) {
    (void)provider; // Suppress unused parameter warning

    if (json_response == NULL || embedding == NULL) {
        return -1;
    }

    cJSON* root = cJSON_Parse(json_response);
    if (root == NULL) {
        fprintf(stderr, "Error: Failed to parse embedding response JSON\n");
        return -1;
    }

    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data == NULL || !cJSON_IsArray(data)) {
        fprintf(stderr, "Error: No data array in embeddings response\n");
        cJSON_Delete(root);
        return -1;
    }

    cJSON* first_item = cJSON_GetArrayItem(data, 0);
    if (first_item == NULL) {
        fprintf(stderr, "Error: Empty data array in embeddings response\n");
        cJSON_Delete(root);
        return -1;
    }

    cJSON* embedding_array = cJSON_GetObjectItem(first_item, "embedding");
    if (embedding_array == NULL || !cJSON_IsArray(embedding_array)) {
        fprintf(stderr, "Error: No embedding array in response\n");
        cJSON_Delete(root);
        return -1;
    }

    size_t count = (size_t)cJSON_GetArraySize(embedding_array);
    if (count == 0) {
        fprintf(stderr, "Error: Empty embedding array\n");
        cJSON_Delete(root);
        return -1;
    }

    embedding->data = malloc(count * sizeof(float));
    if (embedding->data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for embedding\n");
        cJSON_Delete(root);
        return -1;
    }

    size_t i = 0;
    cJSON* value;
    cJSON_ArrayForEach(value, embedding_array) {
        if (cJSON_IsNumber(value)) {
            embedding->data[i++] = (float)value->valuedouble;
        }
    }

    // Set dimension to actual number of values parsed
    embedding->dimension = i;

    if (i == 0) {
        fprintf(stderr, "Error: No valid numeric values in embedding array\n");
        free(embedding->data);
        embedding->data = NULL;
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

// OpenAI embedding provider instance
static EmbeddingProvider openai_embedding_provider = {
    .capabilities = {
        .name = "OpenAI Embeddings",
        .auth_header_format = "Authorization: Bearer %s",
        .requires_auth = 1,
        .default_model = "text-embedding-3-small"
    },
    .detect_provider = openai_embedding_detect_provider,
    .build_request_json = openai_embedding_build_request_json,
    .build_headers = openai_embedding_build_headers,
    .parse_response = openai_embedding_parse_response
};

int register_openai_embedding_provider(EmbeddingProviderRegistry* registry) {
    return register_embedding_provider(registry, &openai_embedding_provider);
}