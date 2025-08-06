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
static size_t openai_embedding_get_dimension(const EmbeddingProvider* provider,
                                            const char* model);

// OpenAI embedding provider implementation
static int openai_embedding_detect_provider(const char* api_url) {
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
    
    cJSON_AddStringToObject(json, "model", model);
    cJSON_AddStringToObject(json, "input", text);
    
    char* json_string = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    
    return json_string;
}

static int openai_embedding_build_headers(const EmbeddingProvider* provider,
                                         const char* api_key,
                                         const char** headers,
                                         int max_headers) {
    (void)provider; // Suppress unused parameter warning
    
    int count = 0;
    static char auth_header[512];
    
    // Add authorization header if API key provided
    if (api_key && count < max_headers - 1) {
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
    
    // Find the data array in the response
    const char *data_start = strstr(json_response, "\"data\"");
    if (data_start == NULL) {
        fprintf(stderr, "Error: No data field in embeddings response\n");
        return -1;
    }
    
    // Find the embedding array
    const char *embedding_start = strstr(data_start, "\"embedding\"");
    if (embedding_start == NULL) {
        fprintf(stderr, "Error: No embedding field in response\n");
        return -1;
    }
    
    // Find the array start
    const char *array_start = strchr(embedding_start, '[');
    if (array_start == NULL) {
        fprintf(stderr, "Error: No embedding array in response\n");
        return -1;
    }
    array_start++; // Skip '['
    
    // Count the number of values
    size_t count = 0;
    const char *p = array_start;
    while (*p != ']' && *p != '\0') {
        char *end;
        strtod(p, &end);
        if (end > p) {
            count++;
            p = end;
            while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
        } else {
            p++;
        }
    }
    
    if (count == 0) {
        fprintf(stderr, "Error: Empty embedding array\n");
        return -1;
    }
    
    // Allocate memory for the embedding
    embedding->data = malloc(count * sizeof(float));
    if (embedding->data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for embedding\n");
        return -1;
    }
    embedding->dimension = count;
    
    // Parse the values
    p = array_start;
    for (size_t i = 0; i < count; i++) {
        char *end;
        embedding->data[i] = (float)strtod(p, &end);
        p = end;
        while (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n') p++;
    }
    
    return 0;
}

static size_t openai_embedding_get_dimension(const EmbeddingProvider* provider,
                                            const char* model) {
    (void)provider; // Suppress unused parameter warning
    
    if (model == NULL) {
        return 1536; // Default for text-embedding-3-small
    }
    
    // Return dimensions for known OpenAI models
    if (strcmp(model, "text-embedding-3-small") == 0) {
        return 1536;
    } else if (strcmp(model, "text-embedding-3-large") == 0) {
        return 3072;
    } else if (strcmp(model, "text-embedding-ada-002") == 0) {
        return 1536;
    }
    
    // Default fallback
    return 1536;
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
    .parse_response = openai_embedding_parse_response,
    .get_dimension = openai_embedding_get_dimension
};

int register_openai_embedding_provider(EmbeddingProviderRegistry* registry) {
    return register_embedding_provider(registry, &openai_embedding_provider);
}