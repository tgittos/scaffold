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

// Helper function to check if a message has tool_use content
static int has_tool_use_id(const char* content, const char* tool_call_id) {
    if (!content || !tool_call_id) return 0;
    
    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "\"id\": \"%s\"", tool_call_id);
    if (strstr(content, search_pattern)) return 1;
    
    snprintf(search_pattern, sizeof(search_pattern), "\"id\":\"%s\"", tool_call_id);
    return strstr(content, search_pattern) != NULL;
}

// Function to filter out orphaned tool results that don't have corresponding tool_use blocks
static void filter_orphaned_tool_results(ConversationHistory* history) {
    if (!history || history->count == 0) return;
    
    int write_idx = 0;
    
    for (int i = 0; i < history->count; i++) {
        ConversationMessage* msg = &history->messages[i];
        
        // If it's a tool result, check if it has a corresponding tool_use in the previous assistant message
        if (strcmp(msg->role, "tool") == 0 && msg->tool_call_id != NULL) {
            int found_tool_use = 0;
            
            // Look backwards for the most recent assistant message
            for (int j = write_idx - 1; j >= 0; j--) {
                ConversationMessage* prev_msg = &history->messages[j];
                
                if (strcmp(prev_msg->role, "assistant") == 0) {
                    if (has_tool_use_id(prev_msg->content, msg->tool_call_id)) {
                        found_tool_use = 1;
                    }
                    break; // Stop at first assistant message
                }
                
                // If we hit a user message, stop looking
                if (strcmp(prev_msg->role, "user") == 0) {
                    break;
                }
            }
            
            if (!found_tool_use) {
                // Skip this orphaned tool result
                free(msg->role);
                free(msg->content);
                free(msg->tool_call_id);
                free(msg->tool_name);
                continue;
            }
        }
        
        // Keep this message
        if (write_idx != i) {
            history->messages[write_idx] = *msg;
        }
        write_idx++;
    }
    
    // Update count to reflect filtered messages
    history->count = write_idx;
}

void init_conversation_history(ConversationHistory *history) {
    if (history == NULL) {
        return;
    }
    
    history->messages = NULL;
    history->count = 0;
    history->capacity = 0;
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
    
    // Load recent conversation from vector database
    document_store_t* store = document_store_get_instance();
    if (store == NULL) {
        // No document store available - return empty history
        return 0;
    }
    
    // Ensure the conversation index exists
    document_store_ensure_index(store, CONVERSATION_INDEX, CONVERSATION_EMBEDDING_DIM, 10000);
    
    // Get recent messages (sliding window)
    time_t now = time(NULL);
    time_t start_time = now - (7 * 24 * 60 * 60); // Last 7 days
    
    document_search_results_t* results = document_store_search_by_time(
        store, CONVERSATION_INDEX, start_time, now, SLIDING_WINDOW_SIZE);
    
    if (results != NULL && results->count > 0) {
        // Sort results by timestamp to ensure chronological order
        qsort(results->documents, results->count, sizeof(document_t*), compare_documents_by_timestamp);
        
        // Add messages to history in chronological order
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
                // Don't save to vector DB again - just add to in-memory history
                if (history->count >= history->capacity) {
                    if (resize_conversation_history(history) != 0) {
                        free(role);
                        free(tool_call_id);
                        free(tool_name);
                        document_store_free_results(results);
                        return -1;
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
        
        // Filter out orphaned tool results to ensure valid sequences
        filter_orphaned_tool_results(history);
    }
    
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
    
    // Get messages from the time range
    document_search_results_t* results = document_store_search_by_time(
        store, CONVERSATION_INDEX, start_time, now, max_messages);
    
    if (results != NULL && results->count > 0) {
        // Sort results by timestamp to ensure chronological order
        qsort(results->documents, results->count, sizeof(document_t*), compare_documents_by_timestamp);
        
        // Add messages to history
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
                // Add to in-memory history without saving to DB again
                if (history->count >= history->capacity) {
                    if (resize_conversation_history(history) != 0) {
                        free(role);
                        free(tool_call_id);
                        free(tool_name);
                        document_store_free_results(results);
                        return -1;
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
        
        // Filter out orphaned tool results to ensure valid sequences
        filter_orphaned_tool_results(history);
    }
    
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
    
    // Filter out orphaned tool results to ensure valid sequences
    filter_orphaned_tool_results(history);
    
    return history;
}