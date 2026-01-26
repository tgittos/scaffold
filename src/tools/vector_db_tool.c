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

int register_vector_db_tool(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    int result;
    
    // 1. Register vector_db_create_index
    ToolParameter create_parameters[6];
    memset(create_parameters, 0, sizeof(create_parameters));

    create_parameters[0].name = strdup("index_name");
    create_parameters[0].type = strdup("string");
    create_parameters[0].description = strdup("Name of the index to create");
    create_parameters[0].enum_values = NULL;
    create_parameters[0].enum_count = 0;
    create_parameters[0].required = 1;
    
    create_parameters[1].name = strdup("dimension");
    create_parameters[1].type = strdup("number");
    create_parameters[1].description = strdup("Dimension of vectors");
    create_parameters[1].enum_values = NULL;
    create_parameters[1].enum_count = 0;
    create_parameters[1].required = 1;
    
    create_parameters[2].name = strdup("max_elements");
    create_parameters[2].type = strdup("number");
    create_parameters[2].description = strdup("Maximum number of elements");
    create_parameters[2].enum_values = NULL;
    create_parameters[2].enum_count = 0;
    create_parameters[2].required = 0;
    
    create_parameters[3].name = strdup("M");
    create_parameters[3].type = strdup("number");
    create_parameters[3].description = strdup("M parameter for HNSW algorithm (default: 16)");
    create_parameters[3].enum_values = NULL;
    create_parameters[3].enum_count = 0;
    create_parameters[3].required = 0;
    
    create_parameters[4].name = strdup("ef_construction");
    create_parameters[4].type = strdup("number");
    create_parameters[4].description = strdup("Construction parameter (default: 200)");
    create_parameters[4].enum_values = NULL;
    create_parameters[4].enum_count = 0;
    create_parameters[4].required = 0;
    
    create_parameters[5].name = strdup("metric");
    create_parameters[5].type = strdup("string");
    create_parameters[5].description = strdup("Distance metric: 'l2', 'cosine', or 'ip' (default: 'l2')");
    create_parameters[5].enum_values = NULL;
    create_parameters[5].enum_count = 0;
    create_parameters[5].required = 0;
    
    for (int i = 0; i < 6; i++) {
        if (create_parameters[i].name == NULL || 
            create_parameters[i].type == NULL ||
            create_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(create_parameters[j].name);
                free(create_parameters[j].type);
                free(create_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_create_index", 
                          "Create a new vector index",
                          create_parameters, 6, execute_vector_db_create_index_tool_call);
    
    for (int i = 0; i < 6; i++) {
        free(create_parameters[i].name);
        free(create_parameters[i].type);
        free(create_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 2. Register vector_db_delete_index
    ToolParameter delete_parameters[1];
    memset(delete_parameters, 0, sizeof(delete_parameters));

    delete_parameters[0].name = strdup("index_name");
    delete_parameters[0].type = strdup("string");
    delete_parameters[0].description = strdup("Name of the index to delete");
    delete_parameters[0].enum_values = NULL;
    delete_parameters[0].enum_count = 0;
    delete_parameters[0].required = 1;
    
    if (delete_parameters[0].name == NULL || 
        delete_parameters[0].type == NULL ||
        delete_parameters[0].description == NULL) {
        free(delete_parameters[0].name);
        free(delete_parameters[0].type);
        free(delete_parameters[0].description);
        return -1;
    }
    
    result = register_tool(registry, "vector_db_delete_index", 
                          "Delete an existing vector index",
                          delete_parameters, 1, execute_vector_db_delete_index_tool_call);
    
    free(delete_parameters[0].name);
    free(delete_parameters[0].type);
    free(delete_parameters[0].description);
    
    if (result != 0) return -1;
    
    // 3. Register vector_db_list_indices
    result = register_tool(registry, "vector_db_list_indices", 
                          "List all vector indices",
                          NULL, 0, execute_vector_db_list_indices_tool_call);
    
    if (result != 0) return -1;
    
    // 4. Register vector_db_add_vector
    ToolParameter add_parameters[3];
    memset(add_parameters, 0, sizeof(add_parameters));

    add_parameters[0].name = strdup("index_name");
    add_parameters[0].type = strdup("string");
    add_parameters[0].description = strdup("Name of the index");
    add_parameters[0].enum_values = NULL;
    add_parameters[0].enum_count = 0;
    add_parameters[0].required = 1;
    
    add_parameters[1].name = strdup("vector");
    add_parameters[1].type = strdup("array");
    add_parameters[1].description = strdup("Vector data as array of numbers");
    add_parameters[1].enum_values = NULL;
    add_parameters[1].enum_count = 0;
    add_parameters[1].required = 1;
    
    add_parameters[2].name = strdup("metadata");
    add_parameters[2].type = strdup("object");
    add_parameters[2].description = strdup("Optional metadata to store with vector");
    add_parameters[2].enum_values = NULL;
    add_parameters[2].enum_count = 0;
    add_parameters[2].required = 0;
    
    for (int i = 0; i < 3; i++) {
        if (add_parameters[i].name == NULL || 
            add_parameters[i].type == NULL ||
            add_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(add_parameters[j].name);
                free(add_parameters[j].type);
                free(add_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_add_vector", 
                          "Add a vector to an index",
                          add_parameters, 3, execute_vector_db_add_vector_tool_call);
    
    for (int i = 0; i < 3; i++) {
        free(add_parameters[i].name);
        free(add_parameters[i].type);
        free(add_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 5. Register vector_db_update_vector
    ToolParameter update_parameters[4];
    memset(update_parameters, 0, sizeof(update_parameters));

    update_parameters[0].name = strdup("index_name");
    update_parameters[0].type = strdup("string");
    update_parameters[0].description = strdup("Name of the index");
    update_parameters[0].enum_values = NULL;
    update_parameters[0].enum_count = 0;
    update_parameters[0].required = 1;
    
    update_parameters[1].name = strdup("label");
    update_parameters[1].type = strdup("number");
    update_parameters[1].description = strdup("Label/ID of the vector to update");
    update_parameters[1].enum_values = NULL;
    update_parameters[1].enum_count = 0;
    update_parameters[1].required = 1;
    
    update_parameters[2].name = strdup("vector");
    update_parameters[2].type = strdup("array");
    update_parameters[2].description = strdup("New vector data");
    update_parameters[2].enum_values = NULL;
    update_parameters[2].enum_count = 0;
    update_parameters[2].required = 1;
    
    update_parameters[3].name = strdup("metadata");
    update_parameters[3].type = strdup("object");
    update_parameters[3].description = strdup("Optional new metadata");
    update_parameters[3].enum_values = NULL;
    update_parameters[3].enum_count = 0;
    update_parameters[3].required = 0;
    
    for (int i = 0; i < 4; i++) {
        if (update_parameters[i].name == NULL || 
            update_parameters[i].type == NULL ||
            update_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(update_parameters[j].name);
                free(update_parameters[j].type);
                free(update_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_update_vector", 
                          "Update an existing vector",
                          update_parameters, 4, execute_vector_db_update_vector_tool_call);
    
    for (int i = 0; i < 4; i++) {
        free(update_parameters[i].name);
        free(update_parameters[i].type);
        free(update_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 6. Register vector_db_delete_vector
    ToolParameter delete_vec_parameters[2];
    memset(delete_vec_parameters, 0, sizeof(delete_vec_parameters));

    delete_vec_parameters[0].name = strdup("index_name");
    delete_vec_parameters[0].type = strdup("string");
    delete_vec_parameters[0].description = strdup("Name of the index");
    delete_vec_parameters[0].enum_values = NULL;
    delete_vec_parameters[0].enum_count = 0;
    delete_vec_parameters[0].required = 1;
    
    delete_vec_parameters[1].name = strdup("label");
    delete_vec_parameters[1].type = strdup("number");
    delete_vec_parameters[1].description = strdup("Label/ID of the vector to delete");
    delete_vec_parameters[1].enum_values = NULL;
    delete_vec_parameters[1].enum_count = 0;
    delete_vec_parameters[1].required = 1;
    
    for (int i = 0; i < 2; i++) {
        if (delete_vec_parameters[i].name == NULL || 
            delete_vec_parameters[i].type == NULL ||
            delete_vec_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(delete_vec_parameters[j].name);
                free(delete_vec_parameters[j].type);
                free(delete_vec_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_delete_vector", 
                          "Delete a vector from an index",
                          delete_vec_parameters, 2, execute_vector_db_delete_vector_tool_call);
    
    for (int i = 0; i < 2; i++) {
        free(delete_vec_parameters[i].name);
        free(delete_vec_parameters[i].type);
        free(delete_vec_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 7. Register vector_db_get_vector
    ToolParameter get_parameters[2];
    memset(get_parameters, 0, sizeof(get_parameters));

    get_parameters[0].name = strdup("index_name");
    get_parameters[0].type = strdup("string");
    get_parameters[0].description = strdup("Name of the index");
    get_parameters[0].enum_values = NULL;
    get_parameters[0].enum_count = 0;
    get_parameters[0].required = 1;
    
    get_parameters[1].name = strdup("label");
    get_parameters[1].type = strdup("number");
    get_parameters[1].description = strdup("Label/ID of the vector to retrieve");
    get_parameters[1].enum_values = NULL;
    get_parameters[1].enum_count = 0;
    get_parameters[1].required = 1;
    
    for (int i = 0; i < 2; i++) {
        if (get_parameters[i].name == NULL || 
            get_parameters[i].type == NULL ||
            get_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(get_parameters[j].name);
                free(get_parameters[j].type);
                free(get_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_get_vector", 
                          "Retrieve a vector by label",
                          get_parameters, 2, execute_vector_db_get_vector_tool_call);
    
    for (int i = 0; i < 2; i++) {
        free(get_parameters[i].name);
        free(get_parameters[i].type);
        free(get_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 8. Register vector_db_search
    ToolParameter search_parameters[3];
    memset(search_parameters, 0, sizeof(search_parameters));

    search_parameters[0].name = strdup("index_name");
    search_parameters[0].type = strdup("string");
    search_parameters[0].description = strdup("Name of the index to search");
    search_parameters[0].enum_values = NULL;
    search_parameters[0].enum_count = 0;
    search_parameters[0].required = 1;
    
    search_parameters[1].name = strdup("query_vector");
    search_parameters[1].type = strdup("array");
    search_parameters[1].description = strdup("Query vector data");
    search_parameters[1].enum_values = NULL;
    search_parameters[1].enum_count = 0;
    search_parameters[1].required = 1;
    
    search_parameters[2].name = strdup("k");
    search_parameters[2].type = strdup("number");
    search_parameters[2].description = strdup("Number of nearest neighbors to return");
    search_parameters[2].enum_values = NULL;
    search_parameters[2].enum_count = 0;
    search_parameters[2].required = 1;
    
    for (int i = 0; i < 3; i++) {
        if (search_parameters[i].name == NULL || 
            search_parameters[i].type == NULL ||
            search_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(search_parameters[j].name);
                free(search_parameters[j].type);
                free(search_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_search", 
                          "Search for nearest neighbors",
                          search_parameters, 3, execute_vector_db_search_tool_call);
    
    for (int i = 0; i < 3; i++) {
        free(search_parameters[i].name);
        free(search_parameters[i].type);
        free(search_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 9. Register vector_db_add_text
    ToolParameter add_text_parameters[3];
    memset(add_text_parameters, 0, sizeof(add_text_parameters));

    add_text_parameters[0].name = strdup("index_name");
    add_text_parameters[0].type = strdup("string");
    add_text_parameters[0].description = strdup("Name of the index");
    add_text_parameters[0].enum_values = NULL;
    add_text_parameters[0].enum_count = 0;
    add_text_parameters[0].required = 1;
    
    add_text_parameters[1].name = strdup("text");
    add_text_parameters[1].type = strdup("string");
    add_text_parameters[1].description = strdup("Text content to embed and store");
    add_text_parameters[1].enum_values = NULL;
    add_text_parameters[1].enum_count = 0;
    add_text_parameters[1].required = 1;
    
    add_text_parameters[2].name = strdup("metadata");
    add_text_parameters[2].type = strdup("object");
    add_text_parameters[2].description = strdup("Optional metadata to store with the text");
    add_text_parameters[2].enum_values = NULL;
    add_text_parameters[2].enum_count = 0;
    add_text_parameters[2].required = 0;
    
    for (int i = 0; i < 3; i++) {
        if (add_text_parameters[i].name == NULL || 
            add_text_parameters[i].type == NULL ||
            add_text_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(add_text_parameters[j].name);
                free(add_text_parameters[j].type);
                free(add_text_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_add_text", 
                          "Add text content to index by generating embeddings",
                          add_text_parameters, 3, execute_vector_db_add_text_tool_call);
    
    for (int i = 0; i < 3; i++) {
        free(add_text_parameters[i].name);
        free(add_text_parameters[i].type);
        free(add_text_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 10. Register vector_db_add_chunked_text
    ToolParameter add_chunked_parameters[5];
    memset(add_chunked_parameters, 0, sizeof(add_chunked_parameters));

    add_chunked_parameters[0].name = strdup("index_name");
    add_chunked_parameters[0].type = strdup("string");
    add_chunked_parameters[0].description = strdup("Name of the index");
    add_chunked_parameters[0].enum_values = NULL;
    add_chunked_parameters[0].enum_count = 0;
    add_chunked_parameters[0].required = 1;
    
    add_chunked_parameters[1].name = strdup("text");
    add_chunked_parameters[1].type = strdup("string");
    add_chunked_parameters[1].description = strdup("Text content to chunk, embed and store");
    add_chunked_parameters[1].enum_values = NULL;
    add_chunked_parameters[1].enum_count = 0;
    add_chunked_parameters[1].required = 1;
    
    add_chunked_parameters[2].name = strdup("max_chunk_size");
    add_chunked_parameters[2].type = strdup("number");
    add_chunked_parameters[2].description = strdup("Maximum size of each chunk (default: 1000)");
    add_chunked_parameters[2].enum_values = NULL;
    add_chunked_parameters[2].enum_count = 0;
    add_chunked_parameters[2].required = 0;
    
    add_chunked_parameters[3].name = strdup("overlap_size");
    add_chunked_parameters[3].type = strdup("number");
    add_chunked_parameters[3].description = strdup("Overlap between chunks (default: 200)");
    add_chunked_parameters[3].enum_values = NULL;
    add_chunked_parameters[3].enum_count = 0;
    add_chunked_parameters[3].required = 0;
    
    add_chunked_parameters[4].name = strdup("metadata");
    add_chunked_parameters[4].type = strdup("object");
    add_chunked_parameters[4].description = strdup("Optional metadata to store with each chunk");
    add_chunked_parameters[4].enum_values = NULL;
    add_chunked_parameters[4].enum_count = 0;
    add_chunked_parameters[4].required = 0;
    
    for (int i = 0; i < 5; i++) {
        if (add_chunked_parameters[i].name == NULL || 
            add_chunked_parameters[i].type == NULL ||
            add_chunked_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(add_chunked_parameters[j].name);
                free(add_chunked_parameters[j].type);
                free(add_chunked_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_add_chunked_text", 
                          "Add long text content by chunking, embedding and storing each chunk",
                          add_chunked_parameters, 5, execute_vector_db_add_chunked_text_tool_call);
    
    for (int i = 0; i < 5; i++) {
        free(add_chunked_parameters[i].name);
        free(add_chunked_parameters[i].type);
        free(add_chunked_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 11. Register vector_db_add_pdf_document
    ToolParameter add_pdf_parameters[4];
    memset(add_pdf_parameters, 0, sizeof(add_pdf_parameters));

    add_pdf_parameters[0].name = strdup("index_name");
    add_pdf_parameters[0].type = strdup("string");
    add_pdf_parameters[0].description = strdup("Name of the index");
    add_pdf_parameters[0].enum_values = NULL;
    add_pdf_parameters[0].enum_count = 0;
    add_pdf_parameters[0].required = 1;
    
    add_pdf_parameters[1].name = strdup("pdf_path");
    add_pdf_parameters[1].type = strdup("string");
    add_pdf_parameters[1].description = strdup("Path to the PDF file to extract, chunk and store");
    add_pdf_parameters[1].enum_values = NULL;
    add_pdf_parameters[1].enum_count = 0;
    add_pdf_parameters[1].required = 1;
    
    add_pdf_parameters[2].name = strdup("max_chunk_size");
    add_pdf_parameters[2].type = strdup("number");
    add_pdf_parameters[2].description = strdup("Maximum size of each chunk (default: 1500)");
    add_pdf_parameters[2].enum_values = NULL;
    add_pdf_parameters[2].enum_count = 0;
    add_pdf_parameters[2].required = 0;
    
    add_pdf_parameters[3].name = strdup("overlap_size");
    add_pdf_parameters[3].type = strdup("number");
    add_pdf_parameters[3].description = strdup("Overlap between chunks (default: 300)");
    add_pdf_parameters[3].enum_values = NULL;
    add_pdf_parameters[3].enum_count = 0;
    add_pdf_parameters[3].required = 0;
    
    for (int i = 0; i < 4; i++) {
        if (add_pdf_parameters[i].name == NULL || 
            add_pdf_parameters[i].type == NULL ||
            add_pdf_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(add_pdf_parameters[j].name);
                free(add_pdf_parameters[j].type);
                free(add_pdf_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_add_pdf_document", 
                          "Extract text from PDF, chunk it, and store chunks as embeddings",
                          add_pdf_parameters, 4, execute_vector_db_add_pdf_document_tool_call);
    
    for (int i = 0; i < 4; i++) {
        free(add_pdf_parameters[i].name);
        free(add_pdf_parameters[i].type);
        free(add_pdf_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 12. Register vector_db_search_text
    ToolParameter search_text_parameters[3];
    memset(search_text_parameters, 0, sizeof(search_text_parameters));

    search_text_parameters[0].name = strdup("index_name");
    search_text_parameters[0].type = strdup("string");
    search_text_parameters[0].description = strdup("Name of the index to search");
    search_text_parameters[0].enum_values = NULL;
    search_text_parameters[0].enum_count = 0;
    search_text_parameters[0].required = 1;
    
    search_text_parameters[1].name = strdup("query");
    search_text_parameters[1].type = strdup("string");
    search_text_parameters[1].description = strdup("Query text to search for");
    search_text_parameters[1].enum_values = NULL;
    search_text_parameters[1].enum_count = 0;
    search_text_parameters[1].required = 1;
    
    search_text_parameters[2].name = strdup("k");
    search_text_parameters[2].type = strdup("number");
    search_text_parameters[2].description = strdup("Number of results to return (default: 5)");
    search_text_parameters[2].enum_values = NULL;
    search_text_parameters[2].enum_count = 0;
    search_text_parameters[2].required = 0;
    
    for (int i = 0; i < 3; i++) {
        if (search_text_parameters[i].name == NULL || 
            search_text_parameters[i].type == NULL ||
            search_text_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(search_text_parameters[j].name);
                free(search_text_parameters[j].type);
                free(search_text_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_search_text", 
                          "Search for similar text content in the vector database",
                          search_text_parameters, 3, execute_vector_db_search_text_tool_call);
    
    for (int i = 0; i < 3; i++) {
        free(search_text_parameters[i].name);
        free(search_text_parameters[i].type);
        free(search_text_parameters[i].description);
    }
    
    if (result != 0) return -1;
    
    // 13. Register vector_db_search_by_time
    ToolParameter search_time_parameters[4];
    memset(search_time_parameters, 0, sizeof(search_time_parameters));

    search_time_parameters[0].name = strdup("index_name");
    search_time_parameters[0].type = strdup("string");
    search_time_parameters[0].description = strdup("Name of the index to search");
    search_time_parameters[0].enum_values = NULL;
    search_time_parameters[0].enum_count = 0;
    search_time_parameters[0].required = 1;
    
    search_time_parameters[1].name = strdup("start_time");
    search_time_parameters[1].type = strdup("number");
    search_time_parameters[1].description = strdup("Start timestamp (Unix epoch, default: 0)");
    search_time_parameters[1].enum_values = NULL;
    search_time_parameters[1].enum_count = 0;
    search_time_parameters[1].required = 0;
    
    search_time_parameters[2].name = strdup("end_time");
    search_time_parameters[2].type = strdup("number");
    search_time_parameters[2].description = strdup("End timestamp (Unix epoch, default: now)");
    search_time_parameters[2].enum_values = NULL;
    search_time_parameters[2].enum_count = 0;
    search_time_parameters[2].required = 0;
    
    search_time_parameters[3].name = strdup("limit");
    search_time_parameters[3].type = strdup("number");
    search_time_parameters[3].description = strdup("Maximum number of results (default: 100)");
    search_time_parameters[3].enum_values = NULL;
    search_time_parameters[3].enum_count = 0;
    search_time_parameters[3].required = 0;
    
    for (int i = 0; i < 4; i++) {
        if (search_time_parameters[i].name == NULL || 
            search_time_parameters[i].type == NULL ||
            search_time_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(search_time_parameters[j].name);
                free(search_time_parameters[j].type);
                free(search_time_parameters[j].description);
            }
            return -1;
        }
    }
    
    result = register_tool(registry, "vector_db_search_by_time", 
                          "Search for documents within a time range",
                          search_time_parameters, 4, execute_vector_db_search_by_time_tool_call);
    
    for (int i = 0; i < 4; i++) {
        free(search_time_parameters[i].name);
        free(search_time_parameters[i].type);
        free(search_time_parameters[i].description);
    }
    
    return result;
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
    for (size_t i = 0; i < chunks->chunks.count; i++) {
        // Add chunk text to document store (it will handle embedding internally)
        int add_result = document_store_add_text(doc_store, index_name, 
                                                chunks->chunks.data[i].text, 
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
                successful_chunks, successful_chunks, failed_chunks, chunks->chunks.count);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response),
                "{\"success\": false, \"error\": \"No chunks were successfully added\", \"failed_chunks\": %zu, \"total_chunks\": %zu}",
                failed_chunks, chunks->chunks.count);
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
    for (size_t i = 0; i < chunks->chunks.count; i++) {
        // Build metadata with PDF info
        char metadata_json[512];
        snprintf(metadata_json, sizeof(metadata_json), 
                "{\"source\": \"pdf\", \"file\": \"%s\", \"page_count\": %d}", 
                pdf_path, pdf_result->page_count);
        
        // Add chunk text to document store (it will handle embedding internally)
        int add_result = document_store_add_text(doc_store, index_name, 
                                                chunks->chunks.data[i].text, 
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
                successful_chunks, successful_chunks, failed_chunks, chunks->chunks.count, pdf_result->page_count);
        result->success = 1;
    } else {
        snprintf(response, sizeof(response),
                "{\"success\": false, \"error\": \"No chunks were successfully added from PDF\", \"failed_chunks\": %zu, \"total_chunks\": %zu, \"pdf_pages\": %d}",
                failed_chunks, chunks->chunks.count, pdf_result->page_count);
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
            cJSON_AddNumberToObject(json, "count", search_results->results.count);

            cJSON* results_array = cJSON_CreateArray();
            if (results_array) {
                for (size_t i = 0; i < search_results->results.count; i++) {
                    document_result_t* res = &search_results->results.data[i];
                    if (res->document) {
                        cJSON* result_item = cJSON_CreateObject();
                        if (result_item) {
                            cJSON_AddNumberToObject(result_item, "id", res->document->id);
                            cJSON_AddStringToObject(result_item, "content", res->document->content ? res->document->content : "");
                            cJSON_AddStringToObject(result_item, "type", res->document->type ? res->document->type : "text");
                            cJSON_AddStringToObject(result_item, "source", res->document->source ? res->document->source : "unknown");
                            cJSON_AddNumberToObject(result_item, "distance", res->distance);
                            cJSON_AddNumberToObject(result_item, "timestamp", res->document->timestamp);

                            if (res->document->metadata_json) {
                                cJSON* metadata = cJSON_Parse(res->document->metadata_json);
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
            cJSON_AddNumberToObject(json, "count", search_results->results.count);

            cJSON* results_array = cJSON_CreateArray();
            if (results_array) {
                for (size_t i = 0; i < search_results->results.count; i++) {
                    document_result_t* res = &search_results->results.data[i];
                    if (res->document) {
                        cJSON* result_item = cJSON_CreateObject();
                        if (result_item) {
                            cJSON_AddNumberToObject(result_item, "id", res->document->id);
                            cJSON_AddStringToObject(result_item, "content", res->document->content ? res->document->content : "");
                            cJSON_AddStringToObject(result_item, "type", res->document->type ? res->document->type : "text");
                            cJSON_AddStringToObject(result_item, "source", res->document->source ? res->document->source : "unknown");
                            cJSON_AddNumberToObject(result_item, "timestamp", res->document->timestamp);

                            if (res->document->metadata_json) {
                                cJSON* metadata = cJSON_Parse(res->document->metadata_json);
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