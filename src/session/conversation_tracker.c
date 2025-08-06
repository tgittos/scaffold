#include "conversation_tracker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include <time.h>
#include "../db/document_store.h"
#include "../llm/embeddings_service.h"

#define INITIAL_CAPACITY 10
#define GROWTH_FACTOR 2
#define CONVERSATION_INDEX "conversations"
#define CONVERSATION_EMBEDDING_DIM 1536
#define SLIDING_WINDOW_SIZE 20  // Number of recent messages to retrieve

// Comparison function for sorting documents by timestamp
static int compare_documents_by_timestamp(const void* a, const void* b) {
    const document_t* doc_a = *(const document_t**)a;
    const document_t* doc_b = *(const document_t**)b;
    
    if (doc_a == NULL && doc_b == NULL) return 0;
    if (doc_a == NULL) return 1;
    if (doc_b == NULL) return -1;
    
    if (doc_a->timestamp < doc_b->timestamp) return -1;
    if (doc_a->timestamp > doc_b->timestamp) return 1;
    return 0;
}


static int resize_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return -1;
    }
    
    int new_capacity = (history->capacity == 0) ? INITIAL_CAPACITY : history->capacity * GROWTH_FACTOR;
    ConversationMessage *new_messages = realloc(history->messages, new_capacity * sizeof(ConversationMessage));
    
    if (new_messages == NULL) {
        return -1;
    }
    
    history->messages = new_messages;
    history->capacity = new_capacity;
    return 0;
}

// Function to load complete conversation turns ensuring no broken tool sequences
static int load_complete_conversation_turns(ConversationHistory* history, time_t start_time, time_t end_time, size_t max_turns) {
    document_store_t* store = document_store_get_instance();
    if (!store) return -1;
    
    // Get all messages in the time range without limit to avoid breaking sequences
    document_search_results_t* results = document_store_search_by_time(
        store, CONVERSATION_INDEX, start_time, end_time, 0); // 0 = no limit
        
    if (!results || results->count == 0) {
        if (results) document_store_free_results(results);
        return 0;
    }
    
    // Sort by timestamp to ensure chronological order
    qsort(results->documents, results->count, sizeof(document_t*), compare_documents_by_timestamp);
    
    // Group messages into conversation turns and select the most recent complete turns
    typedef struct {
        ConversationMessage* messages;
        size_t count;
        size_t capacity;
        time_t start_time;
        time_t end_time;
    } ConversationTurn;
    
    ConversationTurn* turns = NULL;
    size_t turns_count = 0;
    size_t turns_capacity = 0;
    ConversationTurn* current_turn = NULL;
    
    // Process messages and group into conversation turns
    for (size_t i = 0; i < results->count; i++) {
        document_t* doc = results->documents[i];
        if (!doc || !doc->content) continue;
        
        // Parse metadata
        char *role = NULL;
        char *tool_call_id = NULL;
        char *tool_name = NULL;
        
        if (doc->metadata_json) {
            cJSON *metadata = cJSON_Parse(doc->metadata_json);
            if (metadata) {
                cJSON *role_item = cJSON_GetObjectItem(metadata, "role");
                cJSON *tool_call_id_item = cJSON_GetObjectItem(metadata, "tool_call_id");
                cJSON *tool_name_item = cJSON_GetObjectItem(metadata, "tool_name");
                
                if (cJSON_IsString(role_item)) role = strdup(cJSON_GetStringValue(role_item));
                if (cJSON_IsString(tool_call_id_item)) tool_call_id = strdup(cJSON_GetStringValue(tool_call_id_item));
                if (cJSON_IsString(tool_name_item)) tool_name = strdup(cJSON_GetStringValue(tool_name_item));
                
                cJSON_Delete(metadata);
            }
        }
        
        if (!role && doc->source) role = strdup(doc->source);
        if (!role) {
            free(tool_call_id);
            free(tool_name);
            continue;
        }
        
        // Start a new turn on user message
        if (strcmp(role, "user") == 0) {
            // Finish current turn if it exists
            current_turn = NULL;
            
            // Create new turn
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
            current_turn->messages = NULL;
            current_turn->count = 0;
            current_turn->capacity = 0;
            current_turn->start_time = doc->timestamp;
            current_turn->end_time = doc->timestamp;
        }
        
        // Add message to current turn
        if (current_turn) {
            if (current_turn->count >= current_turn->capacity) {
                current_turn->capacity = (current_turn->capacity == 0) ? 4 : current_turn->capacity * 2;
                ConversationMessage* new_messages = realloc(current_turn->messages, 
                    current_turn->capacity * sizeof(ConversationMessage));
                if (!new_messages) {
                    free(role); free(tool_call_id); free(tool_name);
                    goto cleanup;
                }
                current_turn->messages = new_messages;
            }
            
            current_turn->messages[current_turn->count].role = role;
            current_turn->messages[current_turn->count].content = strdup(doc->content);
            current_turn->messages[current_turn->count].tool_call_id = tool_call_id;
            current_turn->messages[current_turn->count].tool_name = tool_name;
            current_turn->count++;
            current_turn->end_time = doc->timestamp;
        } else {
            free(role); free(tool_call_id); free(tool_name);
        }
    }
    
    // Add the most recent complete turns to history
    size_t turns_to_add = (max_turns > 0 && turns_count > max_turns) ? max_turns : turns_count;
    size_t start_turn = (turns_count > turns_to_add) ? turns_count - turns_to_add : 0;
    
    for (size_t t = start_turn; t < turns_count; t++) {
        ConversationTurn* turn = &turns[t];
        
        for (size_t m = 0; m < turn->count; m++) {
            if (history->count >= history->capacity) {
                if (resize_conversation_history(history) != 0) {
                    goto cleanup;
                }
            }
            
            history->messages[history->count] = turn->messages[m];
            history->count++;
            // Don't free the message data since we're transferring ownership
        }
        // Clear the messages array but don't free individual messages
        turn->count = 0;
    }
    
cleanup:
    // Clean up turns (only free the arrays, not individual messages if transferred)
    for (size_t t = 0; t < turns_count; t++) {
        ConversationTurn* turn = &turns[t];
        for (size_t m = 0; m < turn->count; m++) {
            free(turn->messages[m].role);
            free(turn->messages[m].content);
            free(turn->messages[m].tool_call_id);
            free(turn->messages[m].tool_name);
        }
        free(turn->messages);
    }
    free(turns);
    
    document_store_free_results(results);
    return 0;
}

void init_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }
    
    history->messages = NULL;
    history->count = 0;
    history->capacity = 0;
}

// Use unified JSON escaping from json_utils.h

static int add_message_to_history(ConversationHistory *history, const char *role, const char *content, const char *tool_call_id, const char *tool_name) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }
    
    // Resize array if needed
    if (history->count >= history->capacity) {
        if (resize_conversation_history(history) != 0) {
            return -1;
        }
    }
    
    // Allocate and copy role and content
    char *role_copy = strdup(role);
    char *content_copy = strdup(content);
    char *tool_call_id_copy = NULL;
    char *tool_name_copy = NULL;
    
    if (role_copy == NULL || content_copy == NULL) {
        free(role_copy);
        free(content_copy);
        return -1;
    }
    
    // Copy tool metadata if provided
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
    
    history->messages[history->count].role = role_copy;
    history->messages[history->count].content = content_copy;
    history->messages[history->count].tool_call_id = tool_call_id_copy;
    history->messages[history->count].tool_name = tool_name_copy;
    history->count++;
    
    // Also store in vector database
    document_store_t* store = document_store_get_instance();
    if (store != NULL) {
        // Ensure the conversation index exists
        document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);
        
        // Build metadata JSON
        cJSON *metadata = cJSON_CreateObject();
        cJSON_AddStringToObject(metadata, "role", role);
        if (tool_call_id) {
            cJSON_AddStringToObject(metadata, "tool_call_id", tool_call_id);
        }
        if (tool_name) {
            cJSON_AddStringToObject(metadata, "tool_name", tool_name);
        }
        
        char *metadata_json = cJSON_PrintUnformatted(metadata);
        cJSON_Delete(metadata);
        
        if (metadata_json) {
            // Add to document store with embeddings
            document_store_add_text(store, CONVERSATION_INDEX, content, "conversation", role, metadata_json);
            free(metadata_json);
        }
    }
    
    return 0;
}



int load_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return -1;
    }
    
    init_conversation_history(history);
    
    // Load recent conversation from vector database using complete turns
    document_store_t* store = document_store_get_instance();
    if (store == NULL) {
        // No document store available - return empty history
        return 0;
    }
    
    // Ensure the conversation index exists
    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);
    
    // Load complete conversation turns from the last 7 days
    time_t now = time(NULL);
    time_t start_time = now - (7 * 24 * 60 * 60); // Last 7 days
    
    // Load the most recent 10 complete conversation turns (instead of individual messages)
    // This ensures we don't break tool call sequences
    load_complete_conversation_turns(history, start_time, now, 10);
    
    return 0;
}

int append_conversation_message(ConversationHistory *history, const char *role, const char *content) {
    if (history == NULL || role == NULL || content == NULL) {
        return -1;
    }
    
    // Add to in-memory history and vector database
    return add_message_to_history(history, role, content, NULL, NULL);
}

int append_tool_message(ConversationHistory *history, const char *content, const char *tool_call_id, const char *tool_name) {
    if (history == NULL || content == NULL || tool_call_id == NULL || tool_name == NULL) {
        return -1;
    }
    
    // Add to in-memory history and vector database
    return add_message_to_history(history, "tool", content, tool_call_id, tool_name);
}

void cleanup_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }
    
    for (int i = 0; i < history->count; i++) {
        free(history->messages[i].role);
        free(history->messages[i].content);
        free(history->messages[i].tool_call_id);
        free(history->messages[i].tool_name);
        history->messages[i].role = NULL;
        history->messages[i].content = NULL;
        history->messages[i].tool_call_id = NULL;
        history->messages[i].tool_name = NULL;
    }
    
    free(history->messages);
    history->messages = NULL;
    history->count = 0;
    history->capacity = 0;
}

int load_extended_conversation_history(ConversationHistory *history, int days_back, size_t max_messages) {
    if (history == NULL) {
        return -1;
    }
    
    init_conversation_history(history);
    
    document_store_t* store = document_store_get_instance();
    if (store == NULL) {
        return -1;
    }
    
    // Ensure the conversation index exists
    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);
    
    // Calculate time range
    time_t now = time(NULL);
    time_t start_time = 0;
    if (days_back > 0) {
        start_time = now - (days_back * 24 * 60 * 60);
    }
    
    // Convert max_messages to approximate number of conversation turns
    // Assuming average of 3-4 messages per turn (user + assistant + tools)
    size_t max_turns = (max_messages + 3) / 4;
    
    // Load complete conversation turns instead of individual messages
    load_complete_conversation_turns(history, start_time, now, max_turns);
    
    return 0;
}

ConversationHistory* search_conversation_history(const char *query, size_t max_results) {
    if (query == NULL) {
        return NULL;
    }
    
    document_store_t* store = document_store_get_instance();
    if (store == NULL) {
        return NULL;
    }
    
    // Ensure the conversation index exists
    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);
    
    // Search for relevant messages
    document_search_results_t* results = document_store_search_text(
        store, CONVERSATION_INDEX, query, max_results);
    
    if (results == NULL || results->count == 0) {
        if (results) document_store_free_results(results);
        return NULL;
    }
    
    // Create a new conversation history with the search results
    ConversationHistory* history = malloc(sizeof(ConversationHistory));
    if (history == NULL) {
        document_store_free_results(results);
        return NULL;
    }
    
    init_conversation_history(history);
    
    // Sort results by timestamp to ensure chronological order
    qsort(results->documents, results->count, sizeof(document_t*), compare_documents_by_timestamp);
    
    // Add search results to history
    for (size_t i = 0; i < results->count; i++) {
        document_t* doc = results->documents[i];
        if (doc == NULL) continue;
        
        // Parse metadata to get role and tool info
        char *role = NULL;
        char *tool_call_id = NULL;
        char *tool_name = NULL;
        
        if (doc->metadata_json != NULL) {
            cJSON *metadata = cJSON_Parse(doc->metadata_json);
            if (metadata != NULL) {
                cJSON *role_item = cJSON_GetObjectItem(metadata, "role");
                cJSON *tool_call_id_item = cJSON_GetObjectItem(metadata, "tool_call_id");
                cJSON *tool_name_item = cJSON_GetObjectItem(metadata, "tool_name");
                
                if (cJSON_IsString(role_item)) role = strdup(cJSON_GetStringValue(role_item));
                if (cJSON_IsString(tool_call_id_item)) tool_call_id = strdup(cJSON_GetStringValue(tool_call_id_item));
                if (cJSON_IsString(tool_name_item)) tool_name = strdup(cJSON_GetStringValue(tool_name_item));
                
                cJSON_Delete(metadata);
            }
        }
        
        // Use source as role if not in metadata
        if (role == NULL && doc->source != NULL) {
            role = strdup(doc->source);
        }
        
        if (role != NULL && doc->content != NULL) {
            if (history->count >= history->capacity) {
                if (resize_conversation_history(history) != 0) {
                    free(role);
                    free(tool_call_id);
                    free(tool_name);
                    cleanup_conversation_history(history);
                    free(history);
                    document_store_free_results(results);
                    return NULL;
                }
            }
            
            history->messages[history->count].role = role;
            history->messages[history->count].content = strdup(doc->content);
            history->messages[history->count].tool_call_id = tool_call_id;
            history->messages[history->count].tool_name = tool_name;
            history->count++;
        } else {
            free(role);
            free(tool_call_id);
            free(tool_name);
        }
    }
    
    document_store_free_results(results);
    
    // Messages are loaded in chronological order - no filtering needed
    // since search results maintain context
    
    return history;
}