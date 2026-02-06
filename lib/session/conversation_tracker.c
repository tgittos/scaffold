#include "conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include <time.h>
#include "../db/document_store.h"
#include "../db/vector_db_service.h"
#include "../services/services.h"
#include "../util/debug_output.h"

static Services* g_services = NULL;

void conversation_tracker_set_services(Services* services) {
    g_services = services;
}

#define CONVERSATION_INDEX "conversations"
#define CONVERSATION_EMBEDDING_DIM 1536

DARRAY_DEFINE(ConversationHistory, ConversationMessage)

static int compare_results_by_timestamp(const void* a, const void* b) {
    const document_result_t* res_a = (const document_result_t*)a;
    const document_result_t* res_b = (const document_result_t*)b;

    if (res_a->document == NULL && res_b->document == NULL) return 0;
    if (res_a->document == NULL) return 1;
    if (res_b->document == NULL) return -1;

    if (res_a->document->timestamp < res_b->document->timestamp) return -1;
    if (res_a->document->timestamp > res_b->document->timestamp) return 1;
    return 0;
}

typedef struct {
    ConversationMessage* data;
    size_t count;
    size_t capacity;
    time_t start_time;
    time_t end_time;
} ConversationTurn;

static void ConversationTurn_init(ConversationTurn* turn) {
    turn->data = NULL;
    turn->count = 0;
    turn->capacity = 0;
    turn->start_time = 0;
    turn->end_time = 0;
}

static int ConversationTurn_grow(ConversationTurn* turn) {
    size_t new_capacity = (turn->capacity == 0) ? 4 : turn->capacity * 2;
    ConversationMessage* new_data = realloc(turn->data, new_capacity * sizeof(ConversationMessage));
    if (!new_data) return -1;
    turn->data = new_data;
    turn->capacity = new_capacity;
    return 0;
}

static int ConversationTurn_push(ConversationTurn* turn, ConversationMessage msg) {
    if (turn->count >= turn->capacity) {
        if (ConversationTurn_grow(turn) != 0) return -1;
    }
    turn->data[turn->count++] = msg;
    return 0;
}

static void ConversationTurn_destroy(ConversationTurn* turn) {
    for (size_t i = 0; i < turn->count; i++) {
        free(turn->data[i].role);
        free(turn->data[i].content);
        free(turn->data[i].tool_call_id);
        free(turn->data[i].tool_name);
    }
    free(turn->data);
    turn->data = NULL;
    turn->count = 0;
    turn->capacity = 0;
}

// Loads whole conversation turns (user + assistant + tool messages) to avoid
// splitting a tool call sequence across the boundary, which would confuse the LLM.
static int load_complete_conversation_turns(ConversationHistory* history, time_t start_time, time_t end_time, size_t max_turns) {
    document_store_t* store = services_get_document_store(g_services);
    if (!store) return -1;

    // Fetch all messages (limit=0) so we can group into complete turns before truncating
    document_search_results_t* results = document_store_search_by_time(
        store, CONVERSATION_INDEX, start_time, end_time, 0);

    if (!results || results->results.count == 0) {
        if (results) document_store_free_results(results);
        return 0;
    }

    qsort(results->results.data, results->results.count, sizeof(document_result_t), compare_results_by_timestamp);

    ConversationTurn* turns = NULL;
    size_t turns_count = 0;
    size_t turns_capacity = 0;
    ConversationTurn* current_turn = NULL;

    for (size_t i = 0; i < results->results.count; i++) {
        document_t* doc = results->results.data[i].document;
        if (!doc || !doc->content) continue;
        
        char *role = NULL;
        char *tool_call_id = NULL;
        char *tool_name = NULL;
        
        if (doc->metadata_json) {
            cJSON *metadata = cJSON_Parse(doc->metadata_json);
            if (metadata) {
                cJSON *role_item = cJSON_GetObjectItem(metadata, "role");
                cJSON *tool_call_id_item = cJSON_GetObjectItem(metadata, "tool_call_id");
                cJSON *tool_name_item = cJSON_GetObjectItem(metadata, "tool_name");

                if (cJSON_IsString(role_item)) {
                    role = strdup(cJSON_GetStringValue(role_item));
                    if (role == NULL) {
                        cJSON_Delete(metadata);
                        goto cleanup;
                    }
                }
                if (cJSON_IsString(tool_call_id_item)) {
                    tool_call_id = strdup(cJSON_GetStringValue(tool_call_id_item));
                    if (tool_call_id == NULL) {
                        free(role);
                        cJSON_Delete(metadata);
                        goto cleanup;
                    }
                }
                if (cJSON_IsString(tool_name_item)) {
                    tool_name = strdup(cJSON_GetStringValue(tool_name_item));
                    if (tool_name == NULL) {
                        free(role);
                        free(tool_call_id);
                        cJSON_Delete(metadata);
                        goto cleanup;
                    }
                }

                cJSON_Delete(metadata);
            }
        }

        if (!role && doc->source) {
            role = strdup(doc->source);
            if (role == NULL) {
                free(tool_call_id);
                free(tool_name);
                goto cleanup;
            }
        }
        if (!role) {
            free(tool_call_id);
            free(tool_name);
            continue;
        }

        // A new turn begins on each user message, or on an assistant message
        // when no turn is active (e.g., first message in the range).
        if (strcmp(role, "user") == 0 || (strcmp(role, "assistant") == 0 && current_turn == NULL)) {
            current_turn = NULL;
            if (turns_count >= turns_capacity) {
                turns_capacity = (turns_capacity == 0) ? 4 : turns_capacity * 2;
                ConversationTurn* new_turns = realloc(turns, turns_capacity * sizeof(ConversationTurn));
                if (!new_turns) {
                    free(role); free(tool_call_id); free(tool_name);
                    goto cleanup;
                }
                turns = new_turns;
            }

            current_turn = &turns[turns_count++];
            ConversationTurn_init(current_turn);
            current_turn->start_time = doc->timestamp;
            current_turn->end_time = doc->timestamp;
        }

        if (current_turn) {
            char *content_copy = strdup(doc->content);
            if (content_copy == NULL) {
                free(role);
                free(tool_call_id);
                free(tool_name);
                goto cleanup;
            }

            ConversationMessage msg = {
                .role = role,
                .content = content_copy,
                .tool_call_id = tool_call_id,
                .tool_name = tool_name
            };

            if (ConversationTurn_push(current_turn, msg) != 0) {
                free(role);
                free(content_copy);
                free(tool_call_id);
                free(tool_name);
                goto cleanup;
            }

            current_turn->end_time = doc->timestamp;
        } else {
            free(role);
            free(tool_call_id);
            free(tool_name);
        }
    }
    
    size_t turns_to_add = (max_turns > 0 && turns_count > max_turns) ? max_turns : turns_count;
    size_t start_turn = (turns_count > turns_to_add) ? turns_count - turns_to_add : 0;

    for (size_t t = start_turn; t < turns_count; t++) {
        ConversationTurn* turn = &turns[t];

        for (size_t m = 0; m < turn->count; m++) {
            if (ConversationHistory_push(history, turn->data[m]) != 0) {
                // Null out already-transferred messages to prevent double-free in cleanup
                for (size_t j = 0; j < m; j++) {
                    turn->data[j].role = NULL;
                    turn->data[j].content = NULL;
                    turn->data[j].tool_call_id = NULL;
                    turn->data[j].tool_name = NULL;
                }
                goto cleanup;
            }
        }
        // Ownership transferred -- null out pointers so ConversationTurn_destroy
        // won't free memory now owned by history
        for (size_t m = 0; m < turn->count; m++) {
            turn->data[m].role = NULL;
            turn->data[m].content = NULL;
            turn->data[m].tool_call_id = NULL;
            turn->data[m].tool_name = NULL;
        }
    }

cleanup:
    for (size_t t = 0; t < turns_count; t++) {
        ConversationTurn_destroy(&turns[t]);
    }
    free(turns);
    
    document_store_free_results(results);
    return 0;
}

void init_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }

    ConversationHistory_init(history);
}

static int add_message_to_history(ConversationHistory *history, const char *role, const char *content, const char *tool_call_id, const char *tool_name) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }

    char *role_copy = strdup(role);
    char *content_copy = strdup(content);
    char *tool_call_id_copy = NULL;
    char *tool_name_copy = NULL;

    if (role_copy == NULL || content_copy == NULL) {
        free(role_copy);
        free(content_copy);
        return -1;
    }

    if (tool_call_id != NULL) {
        tool_call_id_copy = strdup(tool_call_id);
        if (tool_call_id_copy == NULL) {
            free(role_copy);
            free(content_copy);
            return -1;
        }
    }

    if (tool_name != NULL) {
        tool_name_copy = strdup(tool_name);
        if (tool_name_copy == NULL) {
            free(role_copy);
            free(content_copy);
            free(tool_call_id_copy);
            return -1;
        }
    }

    ConversationMessage msg = {
        .role = role_copy,
        .content = content_copy,
        .tool_call_id = tool_call_id_copy,
        .tool_name = tool_name_copy
    };

    if (ConversationHistory_push(history, msg) != 0) {
        free(role_copy);
        free(content_copy);
        free(tool_call_id_copy);
        free(tool_name_copy);
        return -1;
    }

    // Store in vector database only for user and assistant messages
    // Tool messages are ephemeral context and not worth the embedding cost
    if (strcmp(role, "tool") != 0) {
        // For assistant messages, check if content contains tool_calls
        // and extract only the actual content for storage in long-term memory.
        // Tool calls are ephemeral implementation details that don't need to be remembered.
        const char* content_to_store = content;
        char* extracted_content = NULL;
        int skip_storage = 0;

        if (strcmp(role, "assistant") == 0) {
            cJSON* json = cJSON_Parse(content);
            if (json) {
                cJSON* tool_calls = cJSON_GetObjectItem(json, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    // Strip tool_calls from long-term storage -- only store the
                    // natural language content. Tool calls are ephemeral.
                    cJSON* content_item = cJSON_GetObjectItem(json, "content");
                    if (content_item) {
                        if (cJSON_IsString(content_item)) {
                            const char* content_str = cJSON_GetStringValue(content_item);
                            if (content_str && strlen(content_str) > 0) {
                                extracted_content = strdup(content_str);
                                content_to_store = extracted_content;
                            } else {
                                skip_storage = 1;
                            }
                        } else if (cJSON_IsNull(content_item)) {
                            skip_storage = 1;
                        }
                    } else {
                        skip_storage = 1;
                    }
                }
                cJSON_Delete(json);
            }
        }

        if (!skip_storage) {
            document_store_t* store = services_get_document_store(g_services);
            if (store != NULL) {
                int index_result = document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);
                if (index_result != 0) {
                    debug_printf("Warning: Failed to ensure conversation index\n");
                }

                cJSON *metadata = cJSON_CreateObject();
                cJSON_AddStringToObject(metadata, "role", role);

                char *metadata_json = cJSON_PrintUnformatted(metadata);
                cJSON_Delete(metadata);

                if (metadata_json) {
                    int add_result = vector_db_service_add_text(g_services, CONVERSATION_INDEX, content_to_store, "conversation", role, metadata_json);
                    if (add_result != 0) {
                        debug_printf("Warning: Failed to add conversation message to document store\n");
                    }
                    free(metadata_json);
                }
            }
        }

        free(extracted_content);
    }

    return 0;
}



int load_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return -1;
    }
    
    init_conversation_history(history);
    
    document_store_t* store = services_get_document_store(g_services);
    if (store == NULL) {
        return 0;
    }

    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);

    time_t now = time(NULL);
    time_t start_time = now - (7 * 24 * 60 * 60);

    load_complete_conversation_turns(history, start_time, now, 10);
    
    return 0;
}

int append_conversation_message(ConversationHistory *history, const char *role, const char *content) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }
    
    return add_message_to_history(history, role, content, NULL, NULL);
}

int append_tool_message(ConversationHistory *history, const char *content, const char *tool_call_id, const char *tool_name) {
    if (history == NULL || content == NULL || tool_call_id == NULL || tool_name == NULL) {
        return -1;
    }

    return add_message_to_history(history, "tool", content, tool_call_id, tool_name);
}

void cleanup_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }

    for (size_t i = 0; i < history->count; i++) {
        free(history->data[i].role);
        free(history->data[i].content);
        free(history->data[i].tool_call_id);
        free(history->data[i].tool_name);
    }

    ConversationHistory_destroy(history);
}

int load_extended_conversation_history(ConversationHistory *history, int days_back, size_t max_messages) {
    if (history == NULL) {
        return -1;
    }
    
    init_conversation_history(history);
    
    document_store_t* store = services_get_document_store(g_services);
    if (store == NULL) {
        return -1;
    }
    
    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);

    time_t now = time(NULL);
    time_t start_time = 0;
    if (days_back > 0) {
        start_time = now - (days_back * 24 * 60 * 60);
    }
    
    // Convert message count to turn count assuming ~3-4 messages per turn
    // (user + assistant + tool responses)
    size_t max_turns = (max_messages + 3) / 4;

    load_complete_conversation_turns(history, start_time, now, max_turns);
    
    return 0;
}

ConversationHistory* search_conversation_history(const char *query, size_t max_results) {
    if (query == NULL) {
        return NULL;
    }
    
    document_store_t* store = services_get_document_store(g_services);
    if (store == NULL) {
        return NULL;
    }
    
    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);

    document_search_results_t* results = vector_db_service_search_text(
        g_services, CONVERSATION_INDEX, query, max_results);
    
    if (results == NULL || results->results.count == 0) {
        if (results) document_store_free_results(results);
        return NULL;
    }

    ConversationHistory* history = malloc(sizeof(ConversationHistory));
    if (history == NULL) {
        document_store_free_results(results);
        return NULL;
    }

    init_conversation_history(history);

    qsort(results->results.data, results->results.count, sizeof(document_result_t), compare_results_by_timestamp);

    for (size_t i = 0; i < results->results.count; i++) {
        document_t* doc = results->results.data[i].document;
        if (doc == NULL) continue;
        
        char *role = NULL;
        char *tool_call_id = NULL;
        char *tool_name = NULL;
        
        if (doc->metadata_json != NULL) {
            cJSON *metadata = cJSON_Parse(doc->metadata_json);
            if (metadata != NULL) {
                cJSON *role_item = cJSON_GetObjectItem(metadata, "role");
                cJSON *tool_call_id_item = cJSON_GetObjectItem(metadata, "tool_call_id");
                cJSON *tool_name_item = cJSON_GetObjectItem(metadata, "tool_name");

                if (cJSON_IsString(role_item)) {
                    role = strdup(cJSON_GetStringValue(role_item));
                    if (role == NULL) {
                        cJSON_Delete(metadata);
                        cleanup_conversation_history(history);
                        free(history);
                        document_store_free_results(results);
                        return NULL;
                    }
                }
                if (cJSON_IsString(tool_call_id_item)) {
                    tool_call_id = strdup(cJSON_GetStringValue(tool_call_id_item));
                    if (tool_call_id == NULL) {
                        free(role);
                        cJSON_Delete(metadata);
                        cleanup_conversation_history(history);
                        free(history);
                        document_store_free_results(results);
                        return NULL;
                    }
                }
                if (cJSON_IsString(tool_name_item)) {
                    tool_name = strdup(cJSON_GetStringValue(tool_name_item));
                    if (tool_name == NULL) {
                        free(role);
                        free(tool_call_id);
                        cJSON_Delete(metadata);
                        cleanup_conversation_history(history);
                        free(history);
                        document_store_free_results(results);
                        return NULL;
                    }
                }

                cJSON_Delete(metadata);
            }
        }

        // Fall back to document source field when metadata lacks a role
        if (role == NULL && doc->source != NULL) {
            role = strdup(doc->source);
            if (role == NULL) {
                free(tool_call_id);
                free(tool_name);
                cleanup_conversation_history(history);
                free(history);
                document_store_free_results(results);
                return NULL;
            }
        }

        if (role != NULL && doc->content != NULL) {
            char *content_copy = strdup(doc->content);
            if (content_copy == NULL) {
                free(role);
                free(tool_call_id);
                free(tool_name);
                cleanup_conversation_history(history);
                free(history);
                document_store_free_results(results);
                return NULL;
            }

            ConversationMessage msg = {
                .role = role,
                .content = content_copy,
                .tool_call_id = tool_call_id,
                .tool_name = tool_name
            };

            if (ConversationHistory_push(history, msg) != 0) {
                free(role);
                free(content_copy);
                free(tool_call_id);
                free(tool_name);
                cleanup_conversation_history(history);
                free(history);
                document_store_free_results(results);
                return NULL;
            }
        } else {
            free(role);
            free(tool_call_id);
            free(tool_name);
        }
    }
    
    document_store_free_results(results);
    
    return history;
}