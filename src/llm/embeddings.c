#include "embeddings.h"
#include "../network/http_client.h"
#include "../utils/json_utils.h"
#include "../utils/debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

int embeddings_init(embeddings_config_t *config, const char *model, 
                    const char *api_key, const char *api_url) {
    if (config == NULL || api_key == NULL) {
        return -1;
    }
    
    config->model = safe_strdup(model ? model : "text-embedding-3-small");
    config->api_key = safe_strdup(api_key);
    config->api_url = safe_strdup(api_url ? api_url : "https://api.openai.com/v1/embeddings");
    
    if (config->model == NULL || config->api_key == NULL || config->api_url == NULL) {
        embeddings_cleanup(config);
        return -1;
    }
    
    return 0;
}

static char* build_embeddings_request(const char *model, const char *text) {
    if (model == NULL || text == NULL || strlen(text) == 0) {
        fprintf(stderr, "Error: model and non-empty text parameters are required for embeddings request\n");
        debug_printf("Debug: model=%s, text=%s\n", model ? model : "NULL", text ? text : "NULL");
        return NULL;
    }
    
    JsonBuilder builder;
    if (json_builder_init(&builder) != 0) {
        return NULL;
    }
    
    json_builder_start_object(&builder);
    json_builder_add_string(&builder, "model", model);
    json_builder_add_separator(&builder);
    json_builder_add_string(&builder, "input", text);
    json_builder_end_object(&builder);
    
    return json_builder_finalize(&builder);
}

static int parse_embedding_response(const char *response, embedding_vector_t *embedding) {
    if (response == NULL || embedding == NULL) {
        return -1;
    }
    
    // Find the data array in the response
    const char *data_start = strstr(response, "\"data\"");
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

int embeddings_get_vector(const embeddings_config_t *config, const char *text, 
                          embedding_vector_t *embedding) {
    if (config == NULL || text == NULL || embedding == NULL) {
        return -1;
    }
    
    // Build request JSON
    char *request_json = build_embeddings_request(config->model, text);
    if (request_json == NULL) {
        fprintf(stderr, "Error: Failed to build embeddings request\n");
        return -1;
    }
    
    debug_printf("Embeddings request JSON: %s\n", request_json);
    
    // Set up headers
    const char *headers[10];
    int header_count = 0;
    
    // Don't add Content-Type here - http_client already adds it by default
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
    headers[header_count++] = auth_header;
    headers[header_count] = NULL; // Null-terminate the headers array
    
    // Make API request
    struct HTTPResponse response = {0};
    int result = http_post_with_headers(config->api_url, request_json, headers, &response);
    
    free(request_json);
    
    if (result != 0 || response.data == NULL) {
        fprintf(stderr, "Error: Failed to get embeddings from API\n");
        cleanup_response(&response);
        return -1;
    }
    
    // Check for API errors
    if (strstr(response.data, "\"error\"") != NULL) {
        fprintf(stderr, "API Error: %s\n", response.data);
        cleanup_response(&response);
        return -1;
    }
    
    // Parse the response
    result = parse_embedding_response(response.data, embedding);
    cleanup_response(&response);
    
    return result;
}

void embeddings_free_vector(embedding_vector_t *embedding) {
    if (embedding != NULL && embedding->data != NULL) {
        free(embedding->data);
        embedding->data = NULL;
        embedding->dimension = 0;
    }
}

void embeddings_cleanup(embeddings_config_t *config) {
    if (config != NULL) {
        free(config->model);
        free(config->api_key);
        free(config->api_url);
        config->model = NULL;
        config->api_key = NULL;
        config->api_url = NULL;
    }
}