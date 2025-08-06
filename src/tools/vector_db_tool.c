#include "vector_db_tool.h"
#include "../db/vector_db_service.h"
#include "../db/document_store.h"
#include <cJSON.h>
#include "../utils/document_chunker.h"
#include "../pdf/pdf_extractor.h"
#include "../utils/common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

vector_db_t* get_global_vector_db(void) {
    return vector_db_service_get_database();
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
    
    // 12. Register vector_db_search_text
    ToolParameter *search_text_params = malloc(3 * sizeof(ToolParameter));
    if (search_text_params == NULL) return -1;
    
    search_text_params[0] = (ToolParameter){"index_name", "string", "Name of the index to search", NULL, 0, 1};
    search_text_params[1] = (ToolParameter){"query", "string", "Query text to search for", NULL, 0, 1};
    search_text_params[2] = (ToolParameter){"k", "number", "Number of results to return (default: 5)", NULL, 0, 0};
    
    for (int i = 0; i < 3; i++) {
        search_text_params[i].name = safe_strdup(search_text_params[i].name);
        search_text_params[i].type = safe_strdup(search_text_params[i].type);
        search_text_params[i].description = safe_strdup(search_text_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_search_text", 
                                   "Search for similar text content in the vector database", search_text_params, 3) != 0) {
        return -1;
    }
    
    // 13. Register vector_db_search_by_time
    ToolParameter *search_time_params = malloc(4 * sizeof(ToolParameter));
    if (search_time_params == NULL) return -1;
    
    search_time_params[0] = (ToolParameter){"index_name", "string", "Name of the index to search", NULL, 0, 1};
    search_time_params[1] = (ToolParameter){"start_time", "number", "Start timestamp (Unix epoch, default: 0)", NULL, 0, 0};
    search_time_params[2] = (ToolParameter){"end_time", "number", "End timestamp (Unix epoch, default: now)", NULL, 0, 0};
    search_time_params[3] = (ToolParameter){"limit", "number", "Maximum number of results (default: 100)", NULL, 0, 0};
    
    for (int i = 0; i < 4; i++) {
        search_time_params[i].name = safe_strdup(search_time_params[i].name);
        search_time_params[i].type = safe_strdup(search_time_params[i].type);
        search_time_params[i].description = safe_strdup(search_time_params[i].description);
    }
    
    if (register_single_vector_tool(registry, "vector_db_search_by_time", 
                                   "Search for documents within a time range", search_time_params, 4) != 0) {
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
    
    vector_db_t *db = vector_db_service_get_database();
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
    
    vector_db_t *db = vector_db_service_get_database();
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
    
    vector_db_t *db = vector_db_service_get_database();
    size_t count = 0;
    char **indices = vector_db_list_indices(db, &count);
    
    cJSON* json = cJSON_CreateObject();
    if (!json) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
        result->success = 0;
        return 0;
    }
    
    cJSON_AddBoolToObject(json, "success", cJSON_True);
    
    cJSON* indices_array = cJSON_CreateArray();
    if (!indices_array) {
        cJSON_Delete(json);
        result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
        result->success = 0;
        return 0;
    }
    
    for (size_t i = 0; i < count; i++) {
        cJSON* index_name = cJSON_CreateString(indices[i]);
        if (index_name) {
            cJSON_AddItemToArray(indices_array, index_name);
        }
        free(indices[i]);
    }
    
    cJSON_AddItemToObject(json, "indices", indices_array);
    
    result->result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
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
    
    vector_db_t *db = vector_db_service_get_database();
    
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
    
    vector_db_t *db = vector_db_service_get_database();
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
    
    vector_db_t *db = vector_db_service_get_database();
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
    
    vector_db_t *db = vector_db_service_get_database();
    
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
        cJSON* json = cJSON_CreateObject();
        if (json) {
            cJSON_AddBoolToObject(json, "success", cJSON_True);
            cJSON_AddNumberToObject(json, "label", (double)label);
            
            cJSON* vector_array = cJSON_CreateArray();
            if (vector_array) {
                for (size_t i = 0; i < vec.dimension; i++) {
                    cJSON* val = cJSON_CreateNumber((double)vec.data[i]);
                    if (val) {
                        cJSON_AddItemToArray(vector_array, val);
                    }
                }
                cJSON_AddItemToObject(json, "vector", vector_array);
            }
            
            result->result = cJSON_PrintUnformatted(json);
            cJSON_Delete(json);
            result->success = 1;
        } else {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
            result->success = 0;
        }
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
    
    vector_db_t *db = vector_db_service_get_database();
    search_results_t *results = vector_db_search(db, index_name, &query, (size_t)k);
    
    if (results != NULL) {
        cJSON* json = cJSON_CreateObject();
        if (json) {
            cJSON_AddBoolToObject(json, "success", cJSON_True);
            
            cJSON* results_array = cJSON_CreateArray();
            if (results_array) {
                for (size_t i = 0; i < results->count; i++) {
                    cJSON* result_item = cJSON_CreateObject();
                    if (result_item) {
                        cJSON_AddNumberToObject(result_item, "label", (double)results->results[i].label);
                        cJSON_AddNumberToObject(result_item, "distance", results->results[i].distance);
                        cJSON_AddItemToArray(results_array, result_item);
                    }
                }
                cJSON_AddItemToObject(json, "results", results_array);
            }
            
            result->result = cJSON_PrintUnformatted(json);
            cJSON_Delete(json);
            result->success = 1;
        } else {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
            result->success = 0;
        }
        
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
    
    // Use document store for unified storage
    document_store_t *doc_store = document_store_get_instance();
    
    // Ensure index exists
    if (document_store_ensure_index(doc_store, index_name, 1536, 10000) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to ensure index exists\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    // Add text with embedding to document store
    int add_result = document_store_add_text(doc_store, index_name, text, "text", "api", metadata);
    
    char response[1024];
    if (add_result == 0) {
        size_t doc_count = vector_db_get_index_size(vector_db_service_get_database(), index_name);
        snprintf(response, sizeof(response), 
                "{\"success\": true, \"id\": %zu, \"message\": \"Text embedded and stored successfully\", \"text_preview\": \"%.50s...\"}", 
                doc_count - 1, text);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response), 
                "{\"success\": false, \"error\": \"Failed to store document\"}");
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    
    // Cleanup
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
    
    // Use document store to add chunked text
    document_store_t *doc_store = document_store_get_instance();
    
    // Ensure index exists
    if (document_store_ensure_index(doc_store, index_name, 1536, 10000) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to ensure index exists\"}");
        result->success = 0;
        free_chunking_result(chunks);
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    size_t successful_chunks = 0;
    size_t failed_chunks = 0;
    
    // Process each chunk
    for (size_t i = 0; i < chunks->chunk_count; i++) {
        // Add chunk text to document store (it will handle embedding internally)
        int add_result = document_store_add_text(doc_store, index_name, 
                                                chunks->chunks[i].text, 
                                                "chunk", "api", metadata);
        
        if (add_result == 0) {
            successful_chunks++;
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
    
    // Use document store to add PDF chunks
    document_store_t *doc_store = document_store_get_instance();
    
    // Ensure index exists
    if (document_store_ensure_index(doc_store, index_name, 1536, 10000) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to ensure index exists\"}");
        result->success = 0;
        free_chunking_result(chunks);
        pdf_free_extraction_result(pdf_result);
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
    size_t successful_chunks = 0;
    size_t failed_chunks = 0;
    
    // Process each chunk
    for (size_t i = 0; i < chunks->chunk_count; i++) {
        // Build metadata with PDF info
        char metadata_json[512];
        snprintf(metadata_json, sizeof(metadata_json), 
                "{\"source\": \"pdf\", \"file\": \"%s\", \"page_count\": %d}", 
                pdf_path, pdf_result->page_count);
        
        // Add chunk text to document store (it will handle embedding internally)
        int add_result = document_store_add_text(doc_store, index_name, 
                                                chunks->chunks[i].text, 
                                                "pdf_chunk", "pdf", metadata_json);
        
        if (add_result == 0) {
            successful_chunks++;
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
    free_chunking_result(chunks);
    pdf_free_extraction_result(pdf_result);
    free(index_name);
    free(pdf_path);
    
    return 0;
}

int execute_vector_db_search_text_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    char *query_text = extract_string_param(tool_call->arguments, "query");
    double k = extract_number_param(tool_call->arguments, "k", 5);
    
    if (index_name == NULL || query_text == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required parameters\"}");
        result->success = 0;
        free(index_name);
        free(query_text);
        return 0;
    }
    
    document_store_t *doc_store = document_store_get_instance();
    document_search_results_t *search_results = document_store_search_text(doc_store, index_name, query_text, (size_t)k);
    
    if (search_results != NULL) {
        cJSON* json = cJSON_CreateObject();
        if (json) {
            cJSON_AddBoolToObject(json, "success", cJSON_True);
            cJSON_AddNumberToObject(json, "count", search_results->count);
            
            cJSON* results_array = cJSON_CreateArray();
            if (results_array) {
                for (size_t i = 0; i < search_results->count; i++) {
                    if (search_results->documents[i]) {
                        cJSON* result_item = cJSON_CreateObject();
                        if (result_item) {
                            cJSON_AddNumberToObject(result_item, "id", search_results->documents[i]->id);
                            cJSON_AddStringToObject(result_item, "content", search_results->documents[i]->content ? search_results->documents[i]->content : "");
                            cJSON_AddStringToObject(result_item, "type", search_results->documents[i]->type ? search_results->documents[i]->type : "text");
                            cJSON_AddStringToObject(result_item, "source", search_results->documents[i]->source ? search_results->documents[i]->source : "unknown");
                            if (search_results->distances) {
                                cJSON_AddNumberToObject(result_item, "distance", search_results->distances[i]);
                            }
                            cJSON_AddNumberToObject(result_item, "timestamp", search_results->documents[i]->timestamp);
                            
                            if (search_results->documents[i]->metadata_json) {
                                cJSON* metadata = cJSON_Parse(search_results->documents[i]->metadata_json);
                                if (metadata) {
                                    cJSON_AddItemToObject(result_item, "metadata", metadata);
                                }
                            }
                            
                            cJSON_AddItemToArray(results_array, result_item);
                        }
                    }
                }
                cJSON_AddItemToObject(json, "results", results_array);
            }
            
            result->result = cJSON_PrintUnformatted(json);
            cJSON_Delete(json);
            result->success = 1;
        } else {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
            result->success = 0;
        }
        
        document_store_free_results(search_results);
    } else {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Search failed or no results found\"}");
        result->success = 0;
    }
    
    free(index_name);
    free(query_text);
    return 0;
}

int execute_vector_db_search_by_time_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;
    
    result->tool_call_id = safe_strdup(tool_call->id);
    if (result->tool_call_id == NULL) return -1;
    
    char *index_name = extract_string_param(tool_call->arguments, "index_name");
    double start_time = extract_number_param(tool_call->arguments, "start_time", 0);
    double end_time = extract_number_param(tool_call->arguments, "end_time", time(NULL));
    double limit = extract_number_param(tool_call->arguments, "limit", 100);
    
    if (index_name == NULL) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Missing required index_name\"}");
        result->success = 0;
        free(index_name);
        return 0;
    }
    
    document_store_t *doc_store = document_store_get_instance();
    document_search_results_t *search_results = document_store_search_by_time(doc_store, index_name, 
                                                                             (time_t)start_time, (time_t)end_time, (size_t)limit);
    
    if (search_results != NULL) {
        cJSON* json = cJSON_CreateObject();
        if (json) {
            cJSON_AddBoolToObject(json, "success", cJSON_True);
            cJSON_AddNumberToObject(json, "count", search_results->count);
            
            cJSON* results_array = cJSON_CreateArray();
            if (results_array) {
                for (size_t i = 0; i < search_results->count; i++) {
                    if (search_results->documents[i]) {
                        cJSON* result_item = cJSON_CreateObject();
                        if (result_item) {
                            cJSON_AddNumberToObject(result_item, "id", search_results->documents[i]->id);
                            cJSON_AddStringToObject(result_item, "content", search_results->documents[i]->content ? search_results->documents[i]->content : "");
                            cJSON_AddStringToObject(result_item, "type", search_results->documents[i]->type ? search_results->documents[i]->type : "text");
                            cJSON_AddStringToObject(result_item, "source", search_results->documents[i]->source ? search_results->documents[i]->source : "unknown");
                            cJSON_AddNumberToObject(result_item, "timestamp", search_results->documents[i]->timestamp);
                            
                            if (search_results->documents[i]->metadata_json) {
                                cJSON* metadata = cJSON_Parse(search_results->documents[i]->metadata_json);
                                if (metadata) {
                                    cJSON_AddItemToObject(result_item, "metadata", metadata);
                                }
                            }
                            
                            cJSON_AddItemToArray(results_array, result_item);
                        }
                    }
                }
                cJSON_AddItemToObject(json, "results", results_array);
            }
            
            result->result = cJSON_PrintUnformatted(json);
            cJSON_Delete(json);
            result->success = 1;
        } else {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
            result->success = 0;
        }
        
        document_store_free_results(search_results);
    } else {
        result->result = safe_strdup("{\"success\": false, \"error\": \"No documents found in time range\"}");
        result->success = 0;
    }
    
    free(index_name);
    return 0;
}