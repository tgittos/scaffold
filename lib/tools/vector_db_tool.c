#include "vector_db_tool.h"
#include "tool_param_dsl.h"
#include "db/vector_db_service.h"
#include "db/document_store.h"
#include <cJSON.h>
#include "../util/document_chunker.h"
#include "../pdf/pdf_extractor.h"
#include "../util/common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

vector_db_t *get_global_vector_db(void) {
    return vector_db_service_get_database();
}

static char *document_results_to_json(document_search_results_t *search_results) {
    cJSON *json = cJSON_CreateObject();
    if (!json) return NULL;

    cJSON_AddBoolToObject(json, "success", cJSON_True);
    cJSON_AddNumberToObject(json, "count", search_results->results.count);

    cJSON *results_array = cJSON_CreateArray();
    if (results_array) {
        for (size_t i = 0; i < search_results->results.count; i++) {
            document_result_t *res = &search_results->results.data[i];
            if (!res->document) continue;

            cJSON *item = cJSON_CreateObject();
            if (!item) continue;

            cJSON_AddNumberToObject(item, "id", res->document->id);
            cJSON_AddStringToObject(item, "content", res->document->content ? res->document->content : "");
            cJSON_AddStringToObject(item, "type", res->document->type ? res->document->type : "text");
            cJSON_AddStringToObject(item, "source", res->document->source ? res->document->source : "unknown");
            cJSON_AddNumberToObject(item, "timestamp", res->document->timestamp);
            if (res->distance > 0) {
                cJSON_AddNumberToObject(item, "distance", res->distance);
            }
            if (res->document->metadata_json) {
                cJSON *metadata = cJSON_Parse(res->document->metadata_json);
                if (metadata) cJSON_AddItemToObject(item, "metadata", metadata);
            }
            cJSON_AddItemToArray(results_array, item);
        }
        cJSON_AddItemToObject(json, "results", results_array);
    }

    char *result = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    return result;
}

/* Parameter definitions for vector_db_create_index */
static const ParamDef CREATE_INDEX_PARAMS[] = {
    {"index_name", "string", "Name of the index to create", NULL, 1},
    {"dimension", "number", "Dimension of vectors", NULL, 1},
    {"max_elements", "number", "Maximum number of elements", NULL, 0},
    {"M", "number", "M parameter for HNSW algorithm (default: 16)", NULL, 0},
    {"ef_construction", "number", "Construction parameter (default: 200)", NULL, 0},
    {"metric", "string", "Distance metric: 'l2', 'cosine', or 'ip' (default: 'l2')", NULL, 0},
};

/* Parameter definitions for vector_db_delete_index */
static const ParamDef DELETE_INDEX_PARAMS[] = {
    {"index_name", "string", "Name of the index to delete", NULL, 1},
};

/* Parameter definitions for vector_db_add_vector */
static const ParamDef ADD_VECTOR_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"vector", "array", "Vector data as array of numbers", NULL, 1},
    {"metadata", "object", "Optional metadata to store with vector", NULL, 0},
};

/* Parameter definitions for vector_db_update_vector */
static const ParamDef UPDATE_VECTOR_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"label", "number", "Label/ID of the vector to update", NULL, 1},
    {"vector", "array", "New vector data", NULL, 1},
    {"metadata", "object", "Optional new metadata", NULL, 0},
};

/* Parameter definitions for vector_db_delete_vector */
static const ParamDef DELETE_VECTOR_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"label", "number", "Label/ID of the vector to delete", NULL, 1},
};

/* Parameter definitions for vector_db_get_vector */
static const ParamDef GET_VECTOR_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"label", "number", "Label/ID of the vector to retrieve", NULL, 1},
};

/* Parameter definitions for vector_db_search */
static const ParamDef SEARCH_PARAMS[] = {
    {"index_name", "string", "Name of the index to search", NULL, 1},
    {"query_vector", "array", "Query vector data", NULL, 1},
    {"k", "number", "Number of nearest neighbors to return", NULL, 1},
};

/* Parameter definitions for vector_db_add_text */
static const ParamDef ADD_TEXT_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"text", "string", "Text content to embed and store", NULL, 1},
    {"metadata", "object", "Optional metadata to store with the text", NULL, 0},
};

/* Parameter definitions for vector_db_add_chunked_text */
static const ParamDef ADD_CHUNKED_TEXT_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"text", "string", "Text content to chunk, embed and store", NULL, 1},
    {"max_chunk_size", "number", "Maximum size of each chunk (default: 1000)", NULL, 0},
    {"overlap_size", "number", "Overlap between chunks (default: 200)", NULL, 0},
    {"metadata", "object", "Optional metadata to store with each chunk", NULL, 0},
};

/* Parameter definitions for vector_db_add_pdf_document */
static const ParamDef ADD_PDF_DOCUMENT_PARAMS[] = {
    {"index_name", "string", "Name of the index", NULL, 1},
    {"pdf_path", "string", "Path to the PDF file to extract, chunk and store", NULL, 1},
    {"max_chunk_size", "number", "Maximum size of each chunk (default: 1500)", NULL, 0},
    {"overlap_size", "number", "Overlap between chunks (default: 300)", NULL, 0},
};

/* Parameter definitions for vector_db_search_text */
static const ParamDef SEARCH_TEXT_PARAMS[] = {
    {"index_name", "string", "Name of the index to search", NULL, 1},
    {"query", "string", "Query text to search for", NULL, 1},
    {"k", "number", "Number of results to return (default: 5)", NULL, 0},
};

/* Parameter definitions for vector_db_search_by_time */
static const ParamDef SEARCH_BY_TIME_PARAMS[] = {
    {"index_name", "string", "Name of the index to search", NULL, 1},
    {"start_time", "number", "Start timestamp (Unix epoch, default: 0)", NULL, 0},
    {"end_time", "number", "End timestamp (Unix epoch, default: now)", NULL, 0},
    {"limit", "number", "Maximum number of results (default: 100)", NULL, 0},
};

/* Tool definitions table */
static const ToolDef VECTOR_DB_TOOLS[] = {
    {"vector_db_create_index", "Create a new vector index",
     CREATE_INDEX_PARAMS, 6, execute_vector_db_create_index_tool_call},
    {"vector_db_delete_index", "Delete an existing vector index",
     DELETE_INDEX_PARAMS, 1, execute_vector_db_delete_index_tool_call},
    {"vector_db_list_indices", "List all vector indices",
     NULL, 0, execute_vector_db_list_indices_tool_call},
    {"vector_db_add_vector", "Add a vector to an index",
     ADD_VECTOR_PARAMS, 3, execute_vector_db_add_vector_tool_call},
    {"vector_db_update_vector", "Update an existing vector",
     UPDATE_VECTOR_PARAMS, 4, execute_vector_db_update_vector_tool_call},
    {"vector_db_delete_vector", "Delete a vector from an index",
     DELETE_VECTOR_PARAMS, 2, execute_vector_db_delete_vector_tool_call},
    {"vector_db_get_vector", "Retrieve a vector by label",
     GET_VECTOR_PARAMS, 2, execute_vector_db_get_vector_tool_call},
    {"vector_db_search", "Search for nearest neighbors",
     SEARCH_PARAMS, 3, execute_vector_db_search_tool_call},
    {"vector_db_add_text", "Add text content to index by generating embeddings",
     ADD_TEXT_PARAMS, 3, execute_vector_db_add_text_tool_call},
    {"vector_db_add_chunked_text", "Add long text content by chunking, embedding and storing each chunk",
     ADD_CHUNKED_TEXT_PARAMS, 5, execute_vector_db_add_chunked_text_tool_call},
    {"vector_db_add_pdf_document", "Extract text from PDF, chunk it, and store chunks as embeddings",
     ADD_PDF_DOCUMENT_PARAMS, 4, execute_vector_db_add_pdf_document_tool_call},
    {"vector_db_search_text", "Search for similar text content in the vector database",
     SEARCH_TEXT_PARAMS, 3, execute_vector_db_search_text_tool_call},
    {"vector_db_search_by_time", "Search for documents within a time range",
     SEARCH_BY_TIME_PARAMS, 4, execute_vector_db_search_by_time_tool_call},
};

#define VECTOR_DB_TOOL_COUNT (sizeof(VECTOR_DB_TOOLS) / sizeof(VECTOR_DB_TOOLS[0]))

int register_vector_db_tool(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }
    int registered = register_tools_from_defs(registry, VECTOR_DB_TOOLS, VECTOR_DB_TOOL_COUNT);
    return (registered == VECTOR_DB_TOOL_COUNT) ? 0 : -1;
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
    
    char response[512] = {0};
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

    char response[512] = {0};
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
    
    size_t label = vector_db_get_index_size(db, index_name);
    
    vector_db_error_t err = vector_db_add_vector(db, index_name, &vec, label);
    
    char response[512] = {0};
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
    
    char response[512] = {0};
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
    
    char response[512] = {0};
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
    
    // Pre-allocate at a default dimension; vector_db_get_vector will update if needed
    size_t dimension = 512;
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
        char response[512] = {0};
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
    
    document_store_t *doc_store = document_store_get_instance();

    if (document_store_ensure_index(doc_store, index_name, 1536, 10000) != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to ensure index exists\"}");
        result->success = 0;
        free(index_name);
        free(text);
        free(metadata);
        return 0;
    }
    
    int add_result = document_store_add_text(doc_store, index_name, text, "text", "api", metadata);
    
    char response[1024] = {0};
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
    
    chunking_config_t config = chunker_get_default_config();
    config.max_chunk_size = (size_t)max_chunk_size;
    config.overlap_size = (size_t)overlap_size;
    
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
    
    document_store_t *doc_store = document_store_get_instance();

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

    for (size_t i = 0; i < chunks->chunks.count; i++) {
        int add_result = document_store_add_text(doc_store, index_name, 
                                                chunks->chunks.data[i].text, 
                                                "chunk", "api", metadata);
        
        if (add_result == 0) {
            successful_chunks++;
        } else {
            failed_chunks++;
        }
    }
    
    char response[1024] = {0};
    if (successful_chunks > 0) {
        snprintf(response, sizeof(response),
                "{\"success\": true, \"message\": \"Added %zu chunks successfully\", \"successful_chunks\": %zu, \"failed_chunks\": %zu, \"total_chunks\": %zu}",
                successful_chunks, successful_chunks, failed_chunks, chunks->chunks.count);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response),
                "{\"success\": false, \"error\": \"No chunks were successfully added\", \"failed_chunks\": %zu, \"total_chunks\": %zu}",
                failed_chunks, chunks->chunks.count);
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    
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
    
    if (pdf_extractor_init() != 0) {
        result->result = safe_strdup("{\"success\": false, \"error\": \"Failed to initialize PDF extractor\"}");
        result->success = 0;
        free(index_name);
        free(pdf_path);
        return 0;
    }
    
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
    
    chunking_config_t config = chunker_get_pdf_config();
    config.max_chunk_size = (size_t)max_chunk_size;
    config.overlap_size = (size_t)overlap_size;
    
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
    
    document_store_t *doc_store = document_store_get_instance();

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
    
    for (size_t i = 0; i < chunks->chunks.count; i++) {
        char metadata_json[512];
        snprintf(metadata_json, sizeof(metadata_json),
                "{\"source\": \"pdf\", \"file\": \"%s\", \"page_count\": %d}",
                pdf_path, pdf_result->page_count);

        int add_result = document_store_add_text(doc_store, index_name, 
                                                chunks->chunks.data[i].text, 
                                                "pdf_chunk", "pdf", metadata_json);
        
        if (add_result == 0) {
            successful_chunks++;
        } else {
            failed_chunks++;
        }
    }
    
    char response[1024] = {0};
    if (successful_chunks > 0) {
        snprintf(response, sizeof(response),
                "{\"success\": true, \"message\": \"Processed PDF and added %zu chunks successfully\", \"successful_chunks\": %zu, \"failed_chunks\": %zu, \"total_chunks\": %zu, \"pdf_pages\": %d}",
                successful_chunks, successful_chunks, failed_chunks, chunks->chunks.count, pdf_result->page_count);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response),
                "{\"success\": false, \"error\": \"No chunks were successfully added from PDF\", \"failed_chunks\": %zu, \"total_chunks\": %zu, \"pdf_pages\": %d}",
                failed_chunks, chunks->chunks.count, pdf_result->page_count);
        result->success = 0;
    }
    
    result->result = safe_strdup(response);
    
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
        result->result = document_results_to_json(search_results);
        result->success = result->result ? 1 : 0;
        if (!result->result) {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
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
        result->result = document_results_to_json(search_results);
        result->success = result->result ? 1 : 0;
        if (!result->result) {
            result->result = safe_strdup("{\"success\": false, \"error\": \"Memory allocation failed\"}");
        }
        document_store_free_results(search_results);
    } else {
        result->result = safe_strdup("{\"success\": false, \"error\": \"No documents found in time range\"}");
        result->success = 0;
    }

    free(index_name);
    return 0;
}