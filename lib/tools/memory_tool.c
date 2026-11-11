#include "memory_tool.h"
#include "db/vector_db_service.h"
#include "db/document_store.h"
#include "llm/embeddings_service.h"
#include "services/services.h"
#include "../util/common_utils.h"
#include "../util/json_escape.h"
#include "tool_result_builder.h"
#include "../util/project_scope.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define MEMORY_USER_INDEX "memory_user"
#define MEMORY_INDEX_CAPACITY 100000

static Services* g_services = NULL;
static char g_project_index[PROJECT_SCOPE_NAME_MAX] = "";

const char* memory_tool_project_index(void) {
    return g_project_index;
}

const char* memory_tool_user_index(void) {
    return MEMORY_USER_INDEX;
}

/* NULL selects the project scope. Returns NULL for an unknown scope or
 * when the project root could not be resolved. */
static const char* index_for_scope(const char* scope) {
    if (scope == NULL || strcmp(scope, "project") == 0) {
        return g_project_index[0] != '\0' ? g_project_index : NULL;
    }
    if (strcmp(scope, "user") == 0) return MEMORY_USER_INDEX;
    return NULL;
}

void memory_tool_set_services(Services* services) {
    g_services = services;
}

static int ensure_memory_index(Services* services, const char* index_name) {
    document_store_t* store = services_get_document_store(services);
    if (store == NULL) return -1;
    size_t dim = embeddings_service_get_dimension(services_get_embeddings(services));
    return document_store_ensure_index(store, index_name, dim, MEMORY_INDEX_CAPACITY);
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
    char *scope = extract_string_param(tool_call->arguments, "scope");
    const char *index_name = index_for_scope(scope);

    if (content == NULL) {
        tool_result_builder_set_error(builder, "Missing required parameter: content");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    if (index_name == NULL) {
        tool_result_builder_set_error(builder, "Invalid scope: use 'project' or 'user'");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    embeddings_service_t* embeddings = services_get_embeddings(g_services);
    if (!embeddings_service_is_configured(embeddings)) {
        tool_result_builder_set_error(builder, "Embeddings service not configured. Set embedding_api_url and embedding_model in config, or OPENAI_API_KEY for OpenAI");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        goto cleanup;
    }

    if (ensure_memory_index(g_services, index_name) != 0) {
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

    int add_result = vector_db_service_add_text(g_services, index_name,
                                                 content, "memory", "memory_tool", metadata);

    if (add_result == 0) {
        vector_db_service_t* vdb_svc = services_get_vector_db(g_services);
        size_t memory_id = vector_db_get_index_size(vector_db_service_get_database(vdb_svc), index_name) - 1;

        tool_result_builder_set_success(builder,
            "{\"success\": true, \"memory_id\": %zu, \"scope\": \"%s\", \"message\": \"Memory stored successfully\", \"metadata\": %s}",
            memory_id, index_name == g_project_index ? "project" : "user", metadata);
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
    free(scope);

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

    char* scope = extract_string_param(tool_call->arguments, "scope");
    const char* index_name = index_for_scope(scope);
    free(scope);
    if (index_name == NULL) {
        tool_result_builder_set_error(builder, "Invalid scope: use 'project' or 'user'");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        return 0;
    }

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

    document_t* doc = document_store_get(doc_store, index_name, memory_id);
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

    int delete_result = document_store_delete(doc_store, index_name, memory_id);

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

typedef struct {
    const document_result_t* res;
    const char* scope;
} ScopedResult;

static int compare_by_distance(const void* a, const void* b) {
    float da = ((const ScopedResult*)a)->res->distance;
    float db = ((const ScopedResult*)b)->res->distance;
    return (da > db) - (da < db);
}

static void add_memory_json(cJSON* list, const ScopedResult* item) {
    const document_t* doc = item->res->document;
    cJSON* entry = cJSON_CreateObject();
    if (entry == NULL) return;

    cJSON_AddNumberToObject(entry, "memory_id", (double)doc->id);
    cJSON_AddStringToObject(entry, "scope", item->scope);
    cJSON_AddNumberToObject(entry, "score", 1.0 - item->res->distance);
    if (doc->content != NULL) cJSON_AddStringToObject(entry, "content", doc->content);
    if (doc->type != NULL) cJSON_AddStringToObject(entry, "type", doc->type);
    if (doc->metadata_json != NULL) {
        cJSON* metadata = cJSON_Parse(doc->metadata_json);
        if (metadata != NULL) cJSON_AddItemToObject(entry, "metadata", metadata);
    }
    cJSON_AddItemToArray(list, entry);
}

/* Searches the project and user indices and returns the k closest matches
 * across both as a JSON object string, or NULL on allocation failure. */
static char* recall_from_both_scopes(const char* query, size_t k) {
    static const char* const scopes[2] = {"project", "user"};
    document_search_results_t* found[2] = {NULL, NULL};
    size_t total = 0;

    for (int i = 0; i < 2; i++) {
        const char* index_name = index_for_scope(scopes[i]);
        if (index_name == NULL) continue;
        found[i] = vector_db_service_search_text(g_services, index_name, query, k);
        if (found[i] != NULL) total += found[i]->results.count;
    }

    ScopedResult* merged = total > 0 ? calloc(total, sizeof(*merged)) : NULL;
    size_t n = 0;
    if (merged != NULL) {
        for (int i = 0; i < 2; i++) {
            if (found[i] == NULL) continue;
            for (size_t j = 0; j < found[i]->results.count; j++) {
                if (found[i]->results.data[j].document == NULL) continue;
                merged[n].res = &found[i]->results.data[j];
                merged[n].scope = scopes[i];
                n++;
            }
        }
        qsort(merged, n, sizeof(*merged), compare_by_distance);
    }

    char* json = NULL;
    cJSON* root = cJSON_CreateObject();
    cJSON* list = root ? cJSON_AddArrayToObject(root, "memories") : NULL;
    if (list != NULL && (total == 0 || merged != NULL)) {
        cJSON_AddBoolToObject(root, "success", 1);
        for (size_t i = 0; i < n && i < k; i++) {
            add_memory_json(list, &merged[i]);
        }
        cJSON_AddStringToObject(root, "message",
            n > 0 ? "Found relevant memories" : "No relevant memories found");
        json = cJSON_PrintUnformatted(root);
    }

    cJSON_Delete(root);
    free(merged);
    for (int i = 0; i < 2; i++) {
        if (found[i] != NULL) document_store_free_results(found[i]);
    }
    return json;
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
        tool_result_builder_set_error(builder, "Embeddings service not configured. Set embedding_api_url and embedding_model in config, or OPENAI_API_KEY for OpenAI");
        ToolResult* temp_result = tool_result_builder_finalize(builder);
        if (temp_result) {
            *result = *temp_result;
            free(temp_result);
        }
        free(query);
        return 0;
    }

    char* response = recall_from_both_scopes(query, k > 0 ? (size_t)k : 5);
    if (response == NULL) {
        tool_result_builder_set_error(builder, "Memory allocation failed");
        goto recall_done;
    }
    tool_result_builder_set_success_json(builder, response);
    free(response);

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

    if (project_scope_index_name("memory", g_project_index, sizeof(g_project_index)) != 0) {
        g_project_index[0] = '\0';
    }

    ToolParameter remember_parameters[5];
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

    remember_parameters[4].name = strdup("scope");
    remember_parameters[4].type = strdup("string");
    remember_parameters[4].description = strdup("'project' (default) for facts about the current project; 'user' for preferences that apply in every project");
    remember_parameters[4].enum_values = NULL;
    remember_parameters[4].enum_count = 0;
    remember_parameters[4].required = 0;

    for (int i = 0; i < 5; i++) {
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
                          "Store information in long-term memory for semantic retrieval. "
                          "Use for key facts, user preferences, project conventions, or corrections. "
                          "Do not store transient or easily re-derivable information.",
                          remember_parameters, 5, execute_remember_tool_call);

    for (int i = 0; i < 5; i++) {
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
                          "Search long-term memory using semantic similarity. "
                          "Searches both the current project's memories and user-wide memories; each result reports its scope. "
                          "Use at session start to recall user preferences or context from prior conversations.",
                          recall_parameters, 2, execute_recall_memories_tool_call);

    for (int i = 0; i < 2; i++) {
        free(recall_parameters[i].name);
        free(recall_parameters[i].type);
        free(recall_parameters[i].description);
    }

    if (result != 0) return -1;

    ToolParameter forget_parameters[2];
    memset(forget_parameters, 0, sizeof(forget_parameters));

    forget_parameters[0].name = strdup("memory_id");
    forget_parameters[0].type = strdup("number");
    forget_parameters[0].description = strdup("The ID of the memory to delete");
    forget_parameters[0].enum_values = NULL;
    forget_parameters[0].enum_count = 0;
    forget_parameters[0].required = 1;

    forget_parameters[1].name = strdup("scope");
    forget_parameters[1].type = strdup("string");
    forget_parameters[1].description = strdup("Scope the memory belongs to, as reported by recall_memories: 'project' (default) or 'user'");
    forget_parameters[1].enum_values = NULL;
    forget_parameters[1].enum_count = 0;
    forget_parameters[1].required = 0;

    for (int i = 0; i < 2; i++) {
        if (forget_parameters[i].name == NULL ||
            forget_parameters[i].type == NULL ||
            forget_parameters[i].description == NULL) {
            for (int j = 0; j < 2; j++) {
                free(forget_parameters[j].name);
                free(forget_parameters[j].type);
                free(forget_parameters[j].description);
            }
            return -1;
        }
    }

    result = register_tool(registry, "forget_memory",
                          "Delete a memory from long-term storage by ID. "
                          "Use recall_memories first to find and confirm the ID before deleting.",
                          forget_parameters, 2, execute_forget_memory_tool_call);

    for (int i = 0; i < 2; i++) {
        free(forget_parameters[i].name);
        free(forget_parameters[i].type);
        free(forget_parameters[i].description);
    }

    return result;
}
