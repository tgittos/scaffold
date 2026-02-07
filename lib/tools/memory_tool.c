#include "memory_tool.h"
#include "db/vector_db_service.h"
#include "db/document_store.h"
#include "llm/embeddings_service.h"
#include "services/services.h"
#include "../util/common_utils.h"
#include "../util/json_escape.h"
#include "../util/debug_output.h"
#include "tool_result_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MEMORY_INDEX_NAME "long_term_memory"
#define MEMORY_EMBEDDING_DIM 1536

static Services* g_services = NULL;

void memory_tool_set_services(Services* services) {
    g_services = services;
}

static int ensure_memory_index(Services* services) {
    document_store_t* store = services_get_document_store(services);
    if (store == NULL) return -1;
    return document_store_ensure_index(store, MEMORY_INDEX_NAME, MEMORY_EMBEDDING_DIM, 100000);
}

static char* create_memory_metadata(const char *memory_type, const char *source, const char *importance) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    char *metadata = malloc(1024);
    if (metadata == NULL) return NULL;

    snprintf(metadata, 1024,
        "{\"timestamp\": \"%s\", \"memory_type\": \"%s\", \"source\": \"%s\", \"importance\": \"%s\"}",
        timestamp,
        memory_type ? memory_type : "general",
        source ? source : "conversation",
        importance ? importance : "normal"
    );

    return metadata;
}

int execute_remember_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char *content = extract_string_param(tool_call->arguments, "content");
    char *memory_type = extract_string_param(tool_call->arguments, "type");
    char *source = extract_string_param(tool_call->arguments, "source");
    char *importance = extract_string_param(tool_call->arguments, "importance");

    if (content == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: content");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    embeddings_service_t* embeddings = services_get_embeddings(g_services);
    if (!embeddings_service_is_configured(embeddings)) {
        tool_result_builder_set_error(builder, "Embeddings service not configured. OPENAI_API_KEY environment variable required");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    if (ensure_memory_index(g_services) != 0) {
        tool_result_builder_set_error(builder, "Failed to initialize memory index");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    char *metadata = create_memory_metadata(memory_type, source, importance);
    if (metadata == NULL) {
        tool_result_builder_set_error(builder, "Failed to create metadata");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    int add_result = vector_db_service_add_text(g_services, MEMORY_INDEX_NAME,
                                                 content, "memory", "memory_tool", metadata);

    if (add_result == 0) {
        vector_db_service_t* vdb_svc = services_get_vector_db(g_services);
        size_t memory_id = vector_db_get_index_size(vector_db_service_get_database(vdb_svc), MEMORY_INDEX_NAME) - 1;

        tool_result_builder_set_success(builder,
            "{\"success\": true, \"memory_id\": %zu, \"message\": \"Memory stored successfully\", \"metadata\": %s}",
            memory_id, metadata);
    } else {
        tool_result_builder_set_error(builder, "Failed to store memory");
    }

    ToolResult* temp_result = tool_result_builder_finalize(builder);
    if (temp_result) {
        *result = *temp_result;
        free(temp_result);
    }

    free(metadata);

cleanup:
    free(content);
    free(memory_type);
    free(source);
    free(importance);

    return 0;
}

int execute_forget_memory_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    double memory_id_num = extract_number_param(tool_call->arguments, "memory_id", -1);

    if (memory_id_num < 0) {
        tool_result_builder_set_error(builder, "Missing or invalid required parameter: memory_id");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }

    size_t memory_id = (size_t)memory_id_num;

    document_store_t* doc_store = services_get_document_store(g_services);
    if (doc_store == NULL) {
        tool_result_builder_set_error(builder, "Failed to access document store");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }

    document_t* doc = document_store_get(doc_store, MEMORY_INDEX_NAME, memory_id);
    if (doc == NULL) {
        tool_result_builder_set_error(builder, "Memory with ID %zu not found", memory_id);
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }

    char* content_preview = NULL;
    if (doc->content) {
        size_t len = strlen(doc->content);
        if (len > 50) {
            content_preview = malloc(54);
            if (content_preview) {
                strncpy(content_preview, doc->content, 50);
                strcpy(content_preview + 50, "...");
            }
        } else {
            content_preview = strdup(doc->content);
        }
    }
    document_store_free_document(doc);

    int delete_result = document_store_delete(doc_store, MEMORY_INDEX_NAME, memory_id);

    if (delete_result == 0) {
        char response[1024];
        snprintf(response, sizeof(response),
            "{\"success\": true, \"memory_id\": %zu, \"message\": \"Memory deleted successfully\", \"deleted\": {\"preview\": \"%s\"}}",
            memory_id, content_preview ? content_preview : "");
        tool_result_builder_set_success_json(builder, response);
    } else {
        tool_result_builder_set_error(builder, "Failed to delete memory with ID %zu", memory_id);
    }

    ToolResult* temp_result = tool_result_builder_finalize(builder);
    if (temp_result) {
        *result = *temp_result;
        free(temp_result);
    }

    free(content_preview);

    return 0;
}

int execute_recall_memories_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) return -1;

    tool_result_builder_t* builder = tool_result_builder_create(tool_call->id);
    if (builder == NULL) return -1;

    char *query = extract_string_param(tool_call->arguments, "query");
    double k = extract_number_param(tool_call->arguments, "k", 5);

    if (query == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: query");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }

    embeddings_service_t* embeddings = services_get_embeddings(g_services);
    if (!embeddings_service_is_configured(embeddings)) {
        tool_result_builder_set_error(builder, "Embeddings service not configured. OPENAI_API_KEY environment variable required");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        free(query);
        return 0;
    }

    document_search_results_t* search_results = vector_db_service_search_text(
        g_services, MEMORY_INDEX_NAME, query, (size_t)k);

    if (search_results == NULL || search_results->results.count == 0) {
        tool_result_builder_set_success_json(builder,
            "{\"success\": true, \"memories\": [], \"message\": \"No relevant memories found\"}");
        if (search_results) document_store_free_results(search_results);
        goto recall_done;
    }

    char* response = malloc(65536);
    if (response == NULL) {
        tool_result_builder_set_error(builder, "Memory allocation failed");
        document_store_free_results(search_results);
        goto recall_done;
    }

    strcpy(response, "{\"success\": true, \"memories\": [");
    char *p = response + strlen(response);

    for (size_t i = 0; i < search_results->results.count; i++) {
        document_result_t* res = &search_results->results.data[i];
        if (res->document == NULL) continue;

        if (i > 0) {
            p += sprintf(p, ", ");
        }

        float similarity = 1.0f - res->distance;
        p += sprintf(p, "{\"memory_id\": %zu, \"score\": %.4f",
                     res->document->id, similarity);

        if (res->document->content != NULL) {
            char* escaped_content = json_escape_string(res->document->content);
            if (escaped_content != NULL) {
                p += sprintf(p, ", \"content\": \"%s\"", escaped_content);
                free(escaped_content);
            }
        }

        if (res->document->type != NULL) {
            p += sprintf(p, ", \"type\": \"%s\"", res->document->type);
        }

        if (res->document->metadata_json != NULL) {
            p += sprintf(p, ", \"metadata\": %s", res->document->metadata_json);
        }

        p += sprintf(p, "}");
    }

    strcat(response, "], \"message\": \"Found relevant memories\"}");
    tool_result_builder_set_success_json(builder, response);
    free(response);

    document_store_free_results(search_results);

recall_done:
    {
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
    }

    free(query);

    return 0;
}

int register_memory_tools(ToolRegistry *registry) {
    if (registry == NULL) return -1;
    g_services = registry->services;
    int result;

    ToolParameter remember_parameters[4];
    memset(remember_parameters, 0, sizeof(remember_parameters));

    remember_parameters[0].name = strdup("content");
    remember_parameters[0].type = strdup("string");
    remember_parameters[0].description = strdup("The content to remember");
    remember_parameters[0].enum_values = NULL;
    remember_parameters[0].enum_count = 0;
    remember_parameters[0].required = 1;

    remember_parameters[1].name = strdup("type");
    remember_parameters[1].type = strdup("string");
    remember_parameters[1].description = strdup("Type of memory (e.g., 'user_preference', 'fact', 'instruction', 'correction')");
    remember_parameters[1].enum_values = NULL;
    remember_parameters[1].enum_count = 0;
    remember_parameters[1].required = 0;

    remember_parameters[2].name = strdup("source");
    remember_parameters[2].type = strdup("string");
    remember_parameters[2].description = strdup("Source of the memory (e.g., 'conversation', 'web', 'file')");
    remember_parameters[2].enum_values = NULL;
    remember_parameters[2].enum_count = 0;
    remember_parameters[2].required = 0;

    remember_parameters[3].name = strdup("importance");
    remember_parameters[3].type = strdup("string");
    remember_parameters[3].description = strdup("Importance level: 'low', 'normal', 'high', 'critical'");
    remember_parameters[3].enum_values = NULL;
    remember_parameters[3].enum_count = 0;
    remember_parameters[3].required = 0;

    for (int i = 0; i < 4; i++) {
        if (remember_parameters[i].name == NULL ||
            remember_parameters[i].type == NULL ||
            remember_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(remember_parameters[j].name);
                free(remember_parameters[j].type);
                free(remember_parameters[j].description);
            }
            return -1;
        }
    }

    result = register_tool(registry, "remember",
                          "Store important information in long-term memory for future reference",
                          remember_parameters, 4, execute_remember_tool_call);

    for (int i = 0; i < 4; i++) {
        free(remember_parameters[i].name);
        free(remember_parameters[i].type);
        free(remember_parameters[i].description);
    }

    if (result != 0) return -1;

    ToolParameter recall_parameters[2];
    memset(recall_parameters, 0, sizeof(recall_parameters));

    recall_parameters[0].name = strdup("query");
    recall_parameters[0].type = strdup("string");
    recall_parameters[0].description = strdup("Query to search for relevant memories");
    recall_parameters[0].enum_values = NULL;
    recall_parameters[0].enum_count = 0;
    recall_parameters[0].required = 1;

    recall_parameters[1].name = strdup("k");
    recall_parameters[1].type = strdup("number");
    recall_parameters[1].description = strdup("Number of memories to retrieve (default: 5)");
    recall_parameters[1].enum_values = NULL;
    recall_parameters[1].enum_count = 0;
    recall_parameters[1].required = 0;

    for (int i = 0; i < 2; i++) {
        if (recall_parameters[i].name == NULL ||
            recall_parameters[i].type == NULL ||
            recall_parameters[i].description == NULL) {
            for (int j = 0; j <= i; j++) {
                free(recall_parameters[j].name);
                free(recall_parameters[j].type);
                free(recall_parameters[j].description);
            }
            return -1;
        }
    }

    result = register_tool(registry, "recall_memories",
                          "Search and retrieve relevant memories based on a query",
                          recall_parameters, 2, execute_recall_memories_tool_call);

    for (int i = 0; i < 2; i++) {
        free(recall_parameters[i].name);
        free(recall_parameters[i].type);
        free(recall_parameters[i].description);
    }

    if (result != 0) return -1;

    ToolParameter forget_parameters[1];
    memset(forget_parameters, 0, sizeof(forget_parameters));

    forget_parameters[0].name = strdup("memory_id");
    forget_parameters[0].type = strdup("number");
    forget_parameters[0].description = strdup("The ID of the memory to delete");
    forget_parameters[0].enum_values = NULL;
    forget_parameters[0].enum_count = 0;
    forget_parameters[0].required = 1;

    if (forget_parameters[0].name == NULL ||
        forget_parameters[0].type == NULL ||
        forget_parameters[0].description == NULL) {
        free(forget_parameters[0].name);
        free(forget_parameters[0].type);
        free(forget_parameters[0].description);
        return -1;
    }

    result = register_tool(registry, "forget_memory",
                          "Delete a specific memory from long-term storage by its ID",
                          forget_parameters, 1, execute_forget_memory_tool_call);

    free(forget_parameters[0].name);
    free(forget_parameters[0].type);
    free(forget_parameters[0].description);

    return result;
}
