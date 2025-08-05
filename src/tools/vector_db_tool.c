#include "vector_db_tool.h"
#include "../db/vector_db.h"
#include "../utils/json_utils.h"
#include "../utils/document_chunker.h"
#include "../llm/embeddings.h"
#include "../pdf/pdf_extractor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static vector_db_t *global_vector_db = NULL;

static vector_db_t* get_or_create_vector_db(void) {
    if (global_vector_db == NULL) {
        global_vector_db = vector_db_create();
    }
    return global_vector_db;
}

vector_db_t* get_global_vector_db(void) {
    return get_or_create_vector_db();
}

static char* safe_strdup(const char *str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

static char* extract_string_param(const char *json, const char *param_name) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) {
        return NULL;
    }
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start != '"') return NULL;
    start++; // Skip opening quote
    
    const char *end = start;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\' && *(end + 1) != '\0') {
            end += 2; // Skip escaped character
        } else {
            end++;
        }
    }
    
    if (*end != '"') return NULL;
    
    size_t len = end - start;
    char *result = malloc(len + 1);
    if (result == NULL) return NULL;
    
    memcpy(result, start, len);
    result[len] = '\0';
    
    return result;
}

static double extract_number_param(const char *json, const char *param_name, double default_value) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return default_value;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    char *end;
    double value = strtod(start, &end);
    if (end == start) return default_value;
    
    return value;
}

static int extract_array_numbers(const char *json, const char *param_name, float **out_array, size_t *out_size) {
    char search_key[256] = {0};
    snprintf(search_key, sizeof(search_key), "\"%s\":", param_name);
    
    const char *start = strstr(json, search_key);
    if (start == NULL) return -1;
    
    start += strlen(search_key);
    while (*start == ' ' || *start == '\t') start++;
    
    if (*start != '[') return -1;
    start++; // Skip '['
    
    // Count elements first
    size_t count = 0;
    const char *p = start;
    while (*p != ']' && *p != '\0') {
        char *end;
        strtod(p, &end);
        if (end > p) {
            count++;
            p = end;
            while (*p == ' ' || *p == ',' || *p == '\t') p++;
        } else {
            p++;
        }
    }
    
    if (count == 0) return -1;
    
    *out_array = malloc(count * sizeof(float));
    if (*out_array == NULL) return -1;
    
    *out_size = count;
    
    // Parse values
    p = start;
    for (size_t i = 0; i < count; i++) {
        char *end;
        (*out_array)[i] = (float)strtod(p, &end);
        p = end;
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
    }
    
    return 0;
}

static int register_single_vector_tool(ToolRegistry *registry, const char *name, 
                                      const char *description, ToolParameter *params, int param_count) {
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 1) * sizeof(ToolFunction));
    if (new_functions == NULL) return -1;
    
    registry->functions = new_functions;
    ToolFunction *func = &registry->functions[registry->function_count];
    
    func->name = safe_strdup(name);
    func->description = safe_strdup(description);
    func->parameter_count = param_count;
    func->parameters = params;
    
    if (func->name == NULL || func->description == NULL) {
        free(func->name);
        free(func->description);
        return -1;
    }
    
    registry->function_count++;
    return 0;
}

int register_vector_db_tool(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    
    // 1. Register vector_db_create_index
    ToolParameter *create_params = malloc(6 * sizeof(ToolParameter));
    if (create_params == NULL) return -1;
    
    create_params[0] = (ToolParameter){"index_name", "string", "Name of the index to create", NULL, 0, 1};
    create_params[1] = (ToolParameter){"dimension", "number", "Dimension of vectors", NULL, 0, 1};
    create_params[2] = (ToolParameter){"max_elements", "number", "Maximum number of elements", NULL, 0, 0};
    create_params[3] = (ToolParameter){"M", "number", "M parameter for HNSW algorithm (default: 16)", NULL, 0, 0};
    create_params[4] = (ToolParameter){"ef_construction", "number", "Construction parameter (default: 200)", NULL, 0, 0};
    create_params[5] = (ToolParameter){"metric", "string", "Distance metric: 'l2', 'cosine', or 'ip' (default: 'l2')", NULL, 0, 0};
    
    for (int i = 0; i < 6; i++) {
        create_params[i].name = safe_strdup(create_params[i].name);
        create_params[i].type = safe_strdup(create_params[i].type);
        create_params[i].description = safe_strdup(create_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_create_index", 
                                   "Create a new vector index", create_params, 6) != 0) {
        return -1;
    }
    
    // 2. Register vector_db_delete_index
    ToolParameter *delete_params = malloc(1 * sizeof(ToolParameter));
    if (delete_params == NULL) return -1;
    
    delete_params[0] = (ToolParameter){"index_name", "string", "Name of the index to delete", NULL, 0, 1};
    delete_params[0].name = safe_strdup(delete_params[0].name);
    delete_params[0].type = safe_strdup(delete_params[0].type);
    delete_params[0].description = safe_strdup(delete_params[0].description);
    
    if (register_single_vector_tool(registry, "vector_db_delete_index", 
                                   "Delete an existing vector index", delete_params, 1) != 0) {
        return -1;
    }
    
    // 3. Register vector_db_list_indices
    ToolParameter *list_params = NULL; // No parameters
    
    if (register_single_vector_tool(registry, "vector_db_list_indices", 
                                   "List all vector indices", list_params, 0) != 0) {
        return -1;
    }
    
    // 4. Register vector_db_add_vector
    ToolParameter *add_params = malloc(3 * sizeof(ToolParameter));
    if (add_params == NULL) return -1;
    
    add_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    add_params[1] = (ToolParameter){"vector", "array", "Vector data as array of numbers", NULL, 0, 1};
    add_params[2] = (ToolParameter){"metadata", "object", "Optional metadata to store with vector", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        add_params[i].name = safe_strdup(add_params[i].name);
        add_params[i].type = safe_strdup(add_params[i].type);
        add_params[i].description = safe_strdup(add_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_add_vector", 
                                   "Add a vector to an index", add_params, 3) != 0) {
        return -1;
    }
    
    // 5. Register vector_db_update_vector
    ToolParameter *update_params = malloc(4 * sizeof(ToolParameter));
    if (update_params == NULL) return -1;
    
    update_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    update_params[1] = (ToolParameter){"label", "number", "Label/ID of the vector to update", NULL, 0, 1};
    update_params[2] = (ToolParameter){"vector", "array", "New vector data", NULL, 0, 1};
    update_params[3] = (ToolParameter){"metadata", "object", "Optional new metadata", NULL, 0, 0};
    
    for (int i = 0; i < 4; i++) {
        update_params[i].name = safe_strdup(update_params[i].name);
        update_params[i].type = safe_strdup(update_params[i].type);
        update_params[i].description = safe_strdup(update_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_update_vector", 
                                   "Update an existing vector", update_params, 4) != 0) {
        return -1;
    }
    
    // 6. Register vector_db_delete_vector
    ToolParameter *delete_vec_params = malloc(2 * sizeof(ToolParameter));
    if (delete_vec_params == NULL) return -1;
    
    delete_vec_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    delete_vec_params[1] = (ToolParameter){"label", "number", "Label/ID of the vector to delete", NULL, 0, 1};
    
    for (int i = 0; i < 2; i++) {
        delete_vec_params[i].name = safe_strdup(delete_vec_params[i].name);
        delete_vec_params[i].type = safe_strdup(delete_vec_params[i].type);
        delete_vec_params[i].description = safe_strdup(delete_vec_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_delete_vector", 
                                   "Delete a vector from an index", delete_vec_params, 2) != 0) {
        return -1;
    }
    
    // 7. Register vector_db_get_vector
    ToolParameter *get_params = malloc(2 * sizeof(ToolParameter));
    if (get_params == NULL) return -1;
    
    get_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    get_params[1] = (ToolParameter){"label", "number", "Label/ID of the vector to retrieve", NULL, 0, 1};
    
    for (int i = 0; i < 2; i++) {
        get_params[i].name = safe_strdup(get_params[i].name);
        get_params[i].type = safe_strdup(get_params[i].type);
        get_params[i].description = safe_strdup(get_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_get_vector", 
                                   "Retrieve a vector by label", get_params, 2) != 0) {
        return -1;
    }
    
    // 8. Register vector_db_search
    ToolParameter *search_params = malloc(3 * sizeof(ToolParameter));
    if (search_params == NULL) return -1;
    
    search_params[0] = (ToolParameter){"index_name", "string", "Name of the index to search", NULL, 0, 1};
    search_params[1] = (ToolParameter){"query_vector", "array", "Query vector data", NULL, 0, 1};
    search_params[2] = (ToolParameter){"k", "number", "Number of nearest neighbors to return", NULL, 0, 1};
    
    for (int i = 0; i < 3; i++) {
        search_params[i].name = safe_strdup(search_params[i].name);
        search_params[i].type = safe_strdup(search_params[i].type);
        search_params[i].description = safe_strdup(search_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_search", 
                                   "Search for nearest neighbors", search_params, 3) != 0) {
        return -1;
    }
    
    // 9. Register vector_db_add_text
    ToolParameter *add_text_params = malloc(3 * sizeof(ToolParameter));
    if (add_text_params == NULL) return -1;
    
    add_text_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    add_text_params[1] = (ToolParameter){"text", "string", "Text content to embed and store", NULL, 0, 1};
    add_text_params[2] = (ToolParameter){"metadata", "object", "Optional metadata to store with the text", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        add_text_params[i].name = safe_strdup(add_text_params[i].name);
        add_text_params[i].type = safe_strdup(add_text_params[i].type);
        add_text_params[i].description = safe_strdup(add_text_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_add_text", 
                                   "Add text content to index by generating embeddings", add_text_params, 3) != 0) {
        return -1;
    }
    
    // 10. Register vector_db_add_chunked_text
    ToolParameter *add_chunked_params = malloc(5 * sizeof(ToolParameter));
    if (add_chunked_params == NULL) return -1;
    
    add_chunked_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    add_chunked_params[1] = (ToolParameter){"text", "string", "Text content to chunk, embed and store", NULL, 0, 1};
    add_chunked_params[2] = (ToolParameter){"max_chunk_size", "number", "Maximum size of each chunk (default: 1000)", NULL, 0, 0};
    add_chunked_params[3] = (ToolParameter){"overlap_size", "number", "Overlap between chunks (default: 200)", NULL, 0, 0};
    add_chunked_params[4] = (ToolParameter){"metadata", "object", "Optional metadata to store with each chunk", NULL, 0, 0};
    
    for (int i = 0; i < 5; i++) {
        add_chunked_params[i].name = safe_strdup(add_chunked_params[i].name);
        add_chunked_params[i].type = safe_strdup(add_chunked_params[i].type);
        add_chunked_params[i].description = safe_strdup(add_chunked_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_add_chunked_text", 
                                   "Add long text content by chunking, embedding and storing each chunk", add_chunked_params, 5) != 0) {
        return -1;
    }
    
    // 11. Register vector_db_add_pdf_document
    ToolParameter *add_pdf_params = malloc(4 * sizeof(ToolParameter));
    if (add_pdf_params == NULL) return -1;
    
    add_pdf_params[0] = (ToolParameter){"index_name", "string", "Name of the index", NULL, 0, 1};
    add_pdf_params[1] = (ToolParameter){"pdf_path", "string", "Path to the PDF file to extract, chunk and store", NULL, 0, 1};
    add_pdf_params[2] = (ToolParameter){"max_chunk_size", "number", "Maximum size of each chunk (default: 1500)", NULL, 0, 0};
    add_pdf_params[3] = (ToolParameter){"overlap_size", "number", "Overlap between chunks (default: 300)", NULL, 0, 0};
    
    for (int i = 0; i < 4; i++) {
        add_pdf_params[i].name = safe_strdup(add_pdf_params[i].name);
        add_pdf_params[i].type = safe_strdup(add_pdf_params[i].type);
        add_pdf_params[i].description = safe_strdup(add_pdf_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_add_pdf_document", 
                                   "Extract text from PDF, chunk it, and store chunks as embeddings", add_pdf_params, 4) != 0) {
        return -1;
    }
    
    return 0;
}

int execute_vector_db_create_index_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    double dimension = extract_number_param(tool_call->arguments, "dimension", 0);
    double max_elements = extract_number_param(tool_call->arguments, "max_elements", 10000);
    double M = extract_number_param(tool_call->arguments, "M", 16);
    double ef_construction = extract_number_param(tool_call->arguments, "ef_construction", 200);
    char *metric = extract_string_param(tool_call->arguments, "metric");
    
    if (index_name == NULL || dimension <= 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        free(metric);
        return 0;
    }
    
    vector_db_t *db = get_or_create_vector_db();
    if (db == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to create vector database\"}");
        result->success = 0;
        free(index_name);
        free(metric);
        return 0;
    }
    
    index_config_t config = {
        .dimension = (size_t)dimension,
        .max_elements = (size_t)max_elements,
        .M = (size_t)M,
        .ef_construction = (size_t)ef_construction,
        .random_seed = 42,
        .metric = metric ? metric : "l2"
    };
    
    vector_db_error_t err = vector_db_create_index(db, index_name, &config);
    
    char response[512];
    if (err == VECTOR_DB_OK) {
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"message\": \"Index '%s' created successfully\"}", 
                index_name);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    free(index_name);
    free(metric);
    return 0;
}

int execute_vector_db_delete_index_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    if (index_name == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing index_name\"}");
        result->success = 0;
        return 0;
    }
    
    vector_db_t *db = get_or_create_vector_db();
    vector_db_error_t err = vector_db_delete_index(db, index_name);
    
    char response[512];
    if (err == VECTOR_DB_OK) {
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"message\": \"Index '%s' deleted successfully\"}", 
                index_name);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    free(index_name);
    return 0;
}

int execute_vector_db_list_indices_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    vector_db_t *db = get_or_create_vector_db();
    size_t count = 0;
    char **indices = vector_db_list_indices(db, &count);
    
    JsonBuilder builder;
    if (json_builder_init(&builder) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
        result->success = 0;
        return 0;
    }
    
    json_builder_start_object(&builder);
    json_builder_add_boolean(&builder, "success", 1);
    json_builder_add_separator(&builder);
    json_builder_add_string(&builder, "indices", "[");
    
    for (size_t i = 0; i < count; i++) {
        if (i > 0) json_builder_add_string_no_key(&builder, ", ");
        json_builder_add_string_no_key(&builder, "\"");
        json_builder_add_string_no_key(&builder, indices[i]);
        json_builder_add_string_no_key(&builder, "\"");
        free(indices[i]);
    }
    
    json_builder_add_string_no_key(&builder, "]");
    json_builder_end_object(&builder);
    
    result->result = json_builder_finalize(&builder);
    result->success = 1;
    
    free(indices);
    return 0;
}

int execute_vector_db_add_vector_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    float *vector_data = NULL;
    size_t dimension = 0;
    
    if (index_name == NULL || extract_array_numbers(tool_call->arguments, "vector", &vector_data, &dimension) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    vector_t vec = {
        .data = vector_data,
        .dimension = dimension
    };
    
    vector_db_t *db = get_or_create_vector_db();
    
    // Auto-generate label based on current index size
    size_t label = vector_db_get_index_size(db, index_name);
    
    vector_db_error_t err = vector_db_add_vector(db, index_name, &vec, label);
    
    char response[512];
    if (err == VECTOR_DB_OK) {
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"label\": %zu, \"message\": \"Vector added successfully\"}", 
                label);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    free(vec.data);
    free(index_name);
    return 0;
}

int execute_vector_db_update_vector_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    double label = extract_number_param(tool_call->arguments, "label", -1);
    float *vector_data = NULL;
    size_t dimension = 0;
    
    if (index_name == NULL || label < 0 || extract_array_numbers(tool_call->arguments, "vector", &vector_data, &dimension) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    vector_t vec = {
        .data = vector_data,
        .dimension = dimension
    };
    
    vector_db_t *db = get_or_create_vector_db();
    vector_db_error_t err = vector_db_update_vector(db, index_name, &vec, (size_t)label);
    
    char response[512];
    if (err == VECTOR_DB_OK) {
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"message\": \"Vector updated successfully\"}");
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    free(vec.data);
    free(index_name);
    return 0;
}

int execute_vector_db_delete_vector_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    double label = extract_number_param(tool_call->arguments, "label", -1);
    
    if (index_name == NULL || label < 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    vector_db_t *db = get_or_create_vector_db();
    vector_db_error_t err = vector_db_delete_vector(db, index_name, (size_t)label);
    
    char response[512];
    if (err == VECTOR_DB_OK) {
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"message\": \"Vector deleted successfully\"}");
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    free(index_name);
    return 0;
}

int execute_vector_db_get_vector_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    double label = extract_number_param(tool_call->arguments, "label", -1);
    
    if (index_name == NULL || label < 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    vector_db_t *db = get_or_create_vector_db();
    
    // First get dimension to allocate vector
    size_t dimension = 512; // Default, will be updated
    vector_t vec = {
        .data = malloc(dimension * sizeof(float)),
        .dimension = dimension
    };
    
    if (vec.data == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    vector_db_error_t err = vector_db_get_vector(db, index_name, (size_t)label, &vec);
    
    if (err == VECTOR_DB_OK) {
        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_start_object(&builder);
        json_builder_add_boolean(&builder, "success", 1);
        json_builder_add_separator(&builder);
        json_builder_add_integer(&builder, "label", (int)label);
        json_builder_add_separator(&builder);
        json_builder_add_string(&builder, "vector", "[");
        
        for (size_t i = 0; i < vec.dimension; i++) {
            if (i > 0) json_builder_add_string_no_key(&builder, ", ");
            char val_str[64];
            snprintf(val_str, sizeof(val_str), "%.6f", vec.data[i]);
            json_builder_add_string_no_key(&builder, val_str);
        }
        
        json_builder_add_string_no_key(&builder, "]");
        json_builder_end_object(&builder);
        result->result = json_builder_finalize(&builder);
        result->success = 1;
    } else {
        char response[512];
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->result = safe_strdup(response);
        result->success = 0;
    }
    
    free(vec.data);
    free(index_name);
    return 0;
}

int execute_vector_db_search_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    float *query_data = NULL;
    size_t dimension = 0;
    double k = extract_number_param(tool_call->arguments, "k", 0);
    
    if (index_name == NULL || extract_array_numbers(tool_call->arguments, "query_vector", &query_data, &dimension) != 0 || k <= 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    vector_t query = {
        .data = query_data,
        .dimension = dimension
    };
    
    vector_db_t *db = get_or_create_vector_db();
    search_results_t *results = vector_db_search(db, index_name, &query, (size_t)k);
    
    if (results != NULL) {
        JsonBuilder builder;
        json_builder_init(&builder);
        json_builder_start_object(&builder);
        json_builder_add_boolean(&builder, "success", 1);
        json_builder_add_separator(&builder);
        json_builder_add_string(&builder, "results", "[");
        
        for (size_t i = 0; i < results->count; i++) {
            if (i > 0) json_builder_add_string_no_key(&builder, ", ");
            char result_str[256];
            snprintf(result_str, sizeof(result_str), 
                    "{\"label\": %zu, \"distance\": %.6f}", 
                    results->results[i].label, 
                    results->results[i].distance);
            json_builder_add_string_no_key(&builder, result_str);
        }
        
        json_builder_add_string_no_key(&builder, "]");
        json_builder_end_object(&builder);
        result->result = json_builder_finalize(&builder);
        result->success = 1;
        
        vector_db_free_search_results(results);
    } else {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Search failed\"}");
        result->success = 0;
    }
    
    free(query.data);
    free(index_name);
    return 0;
}

int execute_vector_db_add_text_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    char *text = extract_string_param(tool_call->arguments, "text");
    char *metadata = extract_string_param(tool_call->arguments, "metadata");
    
    if (index_name == NULL || text == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Get API key from environment
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"OPENAI_API_KEY not set\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Initialize embeddings configuration
    embeddings_config_t embeddings_config;
    if (embeddings_init(&embeddings_config, NULL, api_key, NULL) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize embeddings\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Get embedding for the text
    embedding_vector_t embedding;
    if (embeddings_get_vector(&embeddings_config, text, &embedding) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to get embedding from OpenAI\"}");
        result->success = 0;
        embeddings_cleanup(&embeddings_config);
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Convert to vector_t format
    vector_t vec = {
        .data = embedding.data,
        .dimension = embedding.dimension
    };
    
    vector_db_t *db = get_or_create_vector_db();
    
    // Auto-generate label based on current index size
    size_t label = vector_db_get_index_size(db, index_name);
    
    // Add vector to database
    vector_db_error_t err = vector_db_add_vector(db, index_name, &vec, label);
    
    char response[1024];
    if (err == VECTOR_DB_OK) {
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"label\": %zu, \"message\": \"Text embedded and added successfully\", \"dimension\": %zu, \"text_preview\": \"%.50s...\"}", 
                label, embedding.dimension, text);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"%s\"}", 
                vector_db_error_string(err));
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    
    // Cleanup
    embeddings_free_vector(&embedding);
    embeddings_cleanup(&embeddings_config);
    free(index_name);
    free(text);
    free(metadata);
    
    return 0;
}

int execute_vector_db_add_chunked_text_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    char *text = extract_string_param(tool_call->arguments, "text");
    double max_chunk_size = extract_number_param(tool_call->arguments, "max_chunk_size", 1000);
    double overlap_size = extract_number_param(tool_call->arguments, "overlap_size", 200);
    char *metadata = extract_string_param(tool_call->arguments, "metadata");
    
    if (index_name == NULL || text == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Get API key from environment
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"OPENAI_API_KEY not set\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Configure chunking
    chunking_config_t config = chunker_get_default_config();
    config.max_chunk_size = (size_t)max_chunk_size;
    config.overlap_size = (size_t)overlap_size;
    
    // Chunk the document
    chunking_result_t *chunks = chunk_document(text, &config);
    if (!chunks || chunks->error) {
        char error_response[512];
        snprintf(error_response, sizeof(error_response),
                "{\"success\": false, \"error\": \"Chunking failed: %s\"}", 
                chunks ? chunks->error : "Unknown error");
        result->result = safe_strdup(error_response);
        result->success = 0;
        free_chunking_result(chunks);
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Initialize embeddings
    embeddings_config_t embeddings_config;
    if (embeddings_init(&embeddings_config, NULL, api_key, NULL) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize embeddings\"}");
        result->success = 0;
        free_chunking_result(chunks);
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    vector_db_t *db = get_or_create_vector_db();
    size_t successful_chunks = 0;
    size_t failed_chunks = 0;
    
    // Process each chunk
    for (size_t i = 0; i < chunks->chunk_count; i++) {
        // Get embedding for this chunk
        embedding_vector_t embedding;
        if (embeddings_get_vector(&embeddings_config, chunks->chunks[i].text, &embedding) == 0) {
            // Convert to vector_t format
            vector_t vec = {
                .data = embedding.data,
                .dimension = embedding.dimension
            };
            
            // Add to database
            size_t label = vector_db_get_index_size(db, index_name);
            vector_db_error_t err = vector_db_add_vector(db, index_name, &vec, label);
            
            if (err == VECTOR_DB_OK) {
                successful_chunks++;
            } else {
                failed_chunks++;
            }
            
            embeddings_free_vector(&embedding);
        } else {
            failed_chunks++;
        }
    }
    
    // Build response
    char response[1024];
    if (successful_chunks > 0) {
        snprintf(response, sizeof(response),
                "{\"success\": true, \"message\": \"Added %zu chunks successfully\", \"successful_chunks\": %zu, \"failed_chunks\": %zu, \"total_chunks\": %zu}",
                successful_chunks, successful_chunks, failed_chunks, chunks->chunk_count);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response),
                "{\"success\": false, \"error\": \"No chunks were successfully added\", \"failed_chunks\": %zu, \"total_chunks\": %zu}",
                failed_chunks, chunks->chunk_count);
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    
    // Cleanup
    embeddings_cleanup(&embeddings_config);
    free_chunking_result(chunks);
    free(index_name);
    free(text);
    free(metadata);
    
    return 0;
}

int execute_vector_db_add_pdf_document_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    char *pdf_path = extract_string_param(tool_call->arguments, "pdf_path");
    double max_chunk_size = extract_number_param(tool_call->arguments, "max_chunk_size", 1500);
    double overlap_size = extract_number_param(tool_call->arguments, "overlap_size", 300);
    
    if (index_name == NULL || pdf_path == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    // Get API key from environment
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"OPENAI_API_KEY not set\"}");
        result->success = 0;
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    // Initialize PDF extractor
    if (pdf_extractor_init() != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize PDF extractor\"}");
        result->success = 0;
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    // Extract text from PDF
    pdf_extraction_result_t *pdf_result = pdf_extract_text(pdf_path);
    if (!pdf_result || pdf_result->error) {
        char error_response[512];
        snprintf(error_response, sizeof(error_response),
                "{\"success\": false, \"error\": \"PDF extraction failed: %s\"}", 
                pdf_result ? pdf_result->error : "Unknown error");
        result->result = safe_strdup(error_response);
        result->success = 0;
        pdf_free_extraction_result(pdf_result);
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    // Configure chunking for PDF
    chunking_config_t config = chunker_get_pdf_config();
    config.max_chunk_size = (size_t)max_chunk_size;
    config.overlap_size = (size_t)overlap_size;
    
    // Chunk the extracted text
    chunking_result_t *chunks = chunk_document(pdf_result->text, &config);
    if (!chunks || chunks->error) {
        char error_response[512];
        snprintf(error_response, sizeof(error_response),
                "{\"success\": false, \"error\": \"Chunking failed: %s\"}", 
                chunks ? chunks->error : "Unknown error");
        result->result = safe_strdup(error_response);
        result->success = 0;
        free_chunking_result(chunks);
        pdf_free_extraction_result(pdf_result);
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    // Initialize embeddings
    embeddings_config_t embeddings_config;
    if (embeddings_init(&embeddings_config, NULL, api_key, NULL) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize embeddings\"}");
        result->success = 0;
        free_chunking_result(chunks);
        pdf_free_extraction_result(pdf_result);
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    vector_db_t *db = get_or_create_vector_db();
    size_t successful_chunks = 0;
    size_t failed_chunks = 0;
    
    // Process each chunk
    for (size_t i = 0; i < chunks->chunk_count; i++) {
        // Get embedding for this chunk
        embedding_vector_t embedding;
        if (embeddings_get_vector(&embeddings_config, chunks->chunks[i].text, &embedding) == 0) {
            // Convert to vector_t format
            vector_t vec = {
                .data = embedding.data,
                .dimension = embedding.dimension
            };
            
            // Add to database
            size_t label = vector_db_get_index_size(db, index_name);
            vector_db_error_t err = vector_db_add_vector(db, index_name, &vec, label);
            
            if (err == VECTOR_DB_OK) {
                successful_chunks++;
            } else {
                failed_chunks++;
            }
            
            embeddings_free_vector(&embedding);
        } else {
            failed_chunks++;
        }
    }
    
    // Build response
    char response[1024];
    if (successful_chunks > 0) {
        snprintf(response, sizeof(response),
                "{\"success\": true, \"message\": \"Processed PDF and added %zu chunks successfully\", \"successful_chunks\": %zu, \"failed_chunks\": %zu, \"total_chunks\": %zu, \"pdf_pages\": %d}",
                successful_chunks, successful_chunks, failed_chunks, chunks->chunk_count, pdf_result->page_count);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response),
                "{\"success\": false, \"error\": \"No chunks were successfully added from PDF\", \"failed_chunks\": %zu, \"total_chunks\": %zu, \"pdf_pages\": %d}",
                failed_chunks, chunks->chunk_count, pdf_result->page_count);
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    
    // Cleanup
    embeddings_cleanup(&embeddings_config);
    free_chunking_result(chunks);
    pdf_free_extraction_result(pdf_result);
    free(index_name);
    free(pdf_path);
    
    return 0;
}