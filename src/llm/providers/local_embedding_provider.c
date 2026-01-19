#include "../embedding_provider.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations
static int local_embedding_detect_provider(const char* api_url);
static char* local_embedding_build_request_json(const EmbeddingProvider* provider,
                                               const char* model,
                                               const char* text);
static int local_embedding_build_headers(const EmbeddingProvider* provider,
                                        const char* api_key,
                                        const char** headers,
                                        int max_headers);
static int local_embedding_parse_response(const EmbeddingProvider* provider,
                                         const char* json_response,
                                         embedding_vector_t* embedding);
static size_t local_embedding_get_dimension(const EmbeddingProvider* provider,
                                           const char* model);

// Local embedding provider implementation (LMStudio, Ollama, etc.)
static int local_embedding_detect_provider(const char* api_url) {
    // Local embedding is the fallback provider - anything that's not OpenAI
    // This should be checked LAST in the provider registry
    if (api_url == NULL) return 0;
    
    // Explicitly exclude known cloud providers
    if (strstr(api_url, "api.openai.com") != NULL ||
        strstr(api_url, "openai.azure.com") != NULL ||
        strstr(api_url, "api.groq.com") != NULL) {
        return 0;
    }
    
    // Everything else is considered local embedding service
    return 1;
}

static char* local_embedding_build_request_json(const EmbeddingProvider* provider,
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

    // Most local services follow OpenAI format
    if (!cJSON_AddStringToObject(json, "model", model) ||
        !cJSON_AddStringToObject(json, "input", text)) {
        cJSON_Delete(json);
        return NULL;
    }

    char* json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    return json_string;
}

static int local_embedding_build_headers(const EmbeddingProvider* provider,
                                        const char* api_key,
                                        const char** headers,
                                        int max_headers) {
    (void)provider; // Suppress unused parameter warning

    if (max_headers < 1) {
        return 0;
    }

    int count = 0;
    static _Thread_local char auth_header[512];

    // Add authorization header if API key provided (some local servers might require it)
    if (api_key && strlen(api_key) > 0 && count < max_headers - 1) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        headers[count++] = auth_header;
    }

    // Note: Content-Type is handled by http_client automatically

    return count;
}

// Helper to parse embedding array from cJSON
static int parse_embedding_array(cJSON* embedding_array, embedding_vector_t* embedding) {
    if (embedding_array == NULL || !cJSON_IsArray(embedding_array)) {
        return -1;
    }

    size_t count = (size_t)cJSON_GetArraySize(embedding_array);
    if (count == 0) {
        fprintf(stderr, "Error: Empty embedding array\n");
        return -1;
    }

    embedding->data = malloc(count * sizeof(float));
    if (embedding->data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for embedding\n");
        return -1;
    }
    embedding->dimension = count;

    size_t i = 0;
    cJSON* value;
    cJSON_ArrayForEach(value, embedding_array) {
        if (cJSON_IsNumber(value)) {
            embedding->data[i++] = (float)value->valuedouble;
        }
    }

    return 0;
}

static int local_embedding_parse_response(const EmbeddingProvider* provider,
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

    // Try OpenAI-compatible format first: {"data": [{"embedding": [...]}]}
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (data != NULL && cJSON_IsArray(data)) {
        cJSON* first_item = cJSON_GetArrayItem(data, 0);
        if (first_item != NULL) {
            cJSON* embedding_array = cJSON_GetObjectItem(first_item, "embedding");
            if (embedding_array != NULL && cJSON_IsArray(embedding_array)) {
                int result = parse_embedding_array(embedding_array, embedding);
                cJSON_Delete(root);
                return result;
            }
        }
    }

    // Try direct embedding field: {"embedding": [...]}
    cJSON* direct_embedding = cJSON_GetObjectItem(root, "embedding");
    if (direct_embedding != NULL && cJSON_IsArray(direct_embedding)) {
        int result = parse_embedding_array(direct_embedding, embedding);
        cJSON_Delete(root);
        return result;
    }

    // Try if root is directly an array: [...]
    if (cJSON_IsArray(root)) {
        int result = parse_embedding_array(root, embedding);
        cJSON_Delete(root);
        return result;
    }

    fprintf(stderr, "Error: Could not parse embedding response\n");
    cJSON_Delete(root);
    return -1;
}

static size_t local_embedding_get_dimension(const EmbeddingProvider* provider,
                                           const char* model) {
    (void)provider; // Suppress unused parameter warning
    
    if (model == NULL) {
        return 0; // Unknown
    }
    
    // Known dimensions for common local embedding models
    if (strstr(model, "Qwen3-Embedding") != NULL) {
        return 1024; // Qwen3-Embedding-0.6B typical dimension
    } else if (strstr(model, "all-MiniLM") != NULL) {
        return 384;  // all-MiniLM-L6-v2
    } else if (strstr(model, "all-mpnet") != NULL) {
        return 768;  // all-mpnet-base-v2
    }
    
    // Return 0 for unknown - dimension will be determined at runtime
    return 0;
}

// Local embedding provider instance
static EmbeddingProvider local_embedding_provider = {
    .capabilities = {
        .name = "Local Embeddings",
        .auth_header_format = "Authorization: Bearer %s",
        .requires_auth = 0, // Most local services don't require auth
        .default_model = "Qwen3-Embedding-0.6B-Q8_0.gguf"
    },
    .detect_provider = local_embedding_detect_provider,
    .build_request_json = local_embedding_build_request_json,
    .build_headers = local_embedding_build_headers,
    .parse_response = local_embedding_parse_response,
    .get_dimension = local_embedding_get_dimension
};

int register_local_embedding_provider(EmbeddingProviderRegistry* registry) {
    return register_embedding_provider(registry, &local_embedding_provider);
}