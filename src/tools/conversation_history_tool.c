#include "conversation_history_tool.h"
#include "../session/conversation_tracker.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int execute_get_conversation_history_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }
    
    // Parse arguments
    int days_back = 7; // Default to last 7 days
    size_t max_messages = 100; // Default to 100 messages
    
    if (tool_call->arguments != NULL) {
        cJSON *args = cJSON_Parse(tool_call->arguments);
        if (args != NULL) {
            cJSON *days_item = cJSON_GetObjectItem(args, "days_back");
            if (cJSON_IsNumber(days_item)) {
                days_back = (int)cJSON_GetNumberValue(days_item);
            }
            
            cJSON *max_item = cJSON_GetObjectItem(args, "max_messages");
            if (cJSON_IsNumber(max_item)) {
                max_messages = (size_t)cJSON_GetNumberValue(max_item);
            }
            
            cJSON_Delete(args);
        }
    }
    
    // Load extended conversation history
    ConversationHistory history;
    init_conversation_history(&history);
    
    if (load_extended_conversation_history(&history, days_back, max_messages) != 0) {
        result->success = 0;
        result->result = strdup("Failed to load conversation history");
        return 0;
    }
    
    // Convert to JSON
    cJSON *messages_array = cJSON_CreateArray();
    for (int i = 0; i < history.count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", history.messages[i].role);
        cJSON_AddStringToObject(msg, "content", history.messages[i].content);
        
        if (history.messages[i].tool_call_id != NULL) {
            cJSON_AddStringToObject(msg, "tool_call_id", history.messages[i].tool_call_id);
        }
        if (history.messages[i].tool_name != NULL) {
            cJSON_AddStringToObject(msg, "tool_name", history.messages[i].tool_name);
        }
        
        cJSON_AddItemToArray(messages_array, msg);
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "message_count", history.count);
    cJSON_AddItemToObject(response, "messages", messages_array);
    
    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    cleanup_conversation_history(&history);
    
    if (json_str == NULL) {
        result->success = 0;
        result->result = strdup("Failed to serialize conversation history");
        return 0;
    }
    
    result->success = 1;
    result->result = json_str;
    return 0;
}

int execute_search_conversation_tool_call(const ToolCall *tool_call, ToolResult *result) {
    if (tool_call == NULL || result == NULL) {
        return -1;
    }
    
    // Parse arguments
    char *query = NULL;
    size_t max_results = 10; // Default to 10 results
    
    if (tool_call->arguments != NULL) {
        cJSON *args = cJSON_Parse(tool_call->arguments);
        if (args != NULL) {
            cJSON *query_item = cJSON_GetObjectItem(args, "query");
            if (cJSON_IsString(query_item)) {
                query = strdup(cJSON_GetStringValue(query_item));
            }
            
            cJSON *max_item = cJSON_GetObjectItem(args, "max_results");
            if (cJSON_IsNumber(max_item)) {
                max_results = (size_t)cJSON_GetNumberValue(max_item);
            }
            
            cJSON_Delete(args);
        }
    }
    
    if (query == NULL) {
        result->success = 0;
        result->result = strdup("Query parameter is required");
        return 0;
    }
    
    // Search conversation history
    ConversationHistory *history = search_conversation_history(query, max_results);
    
    if (history == NULL) {
        result->success = 1;
        result->result = strdup("{\"message_count\": 0, \"messages\": []}");
        free(query);
        return 0;
    }
    
    // Convert to JSON
    cJSON *messages_array = cJSON_CreateArray();
    for (int i = 0; i < history->count; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", history->messages[i].role);
        cJSON_AddStringToObject(msg, "content", history->messages[i].content);
        
        if (history->messages[i].tool_call_id != NULL) {
            cJSON_AddStringToObject(msg, "tool_call_id", history->messages[i].tool_call_id);
        }
        if (history->messages[i].tool_name != NULL) {
            cJSON_AddStringToObject(msg, "tool_name", history->messages[i].tool_name);
        }
        
        cJSON_AddItemToArray(messages_array, msg);
    }
    
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "message_count", history->count);
    cJSON_AddStringToObject(response, "query", query);
    cJSON_AddItemToObject(response, "messages", messages_array);
    
    char *json_str = cJSON_Print(response);
    cJSON_Delete(response);
    
    cleanup_conversation_history(history);
    free(history);
    free(query);
    
    if (json_str == NULL) {
        result->success = 0;
        result->result = strdup("Failed to serialize search results");
        return 0;
    }
    
    result->success = 1;
    result->result = json_str;
    return 0;
}

// Helper function to safely duplicate strings
static char* safe_strdup(const char* str) {
    if (str == NULL) return NULL;
    return strdup(str);
}

int register_conversation_history_tool(ToolRegistry *registry) {
    if (registry == NULL) {
        return -1;
    }
    
    // Resize the functions array to accommodate 2 more tools
    ToolFunction *new_functions = realloc(registry->functions, 
                                         (registry->function_count + 2) * sizeof(ToolFunction));
    if (new_functions == NULL) return -1;
    
    registry->functions = new_functions;
    int current_count = registry->function_count;
    
    // 1. Register get_conversation_history tool
    ToolParameter *get_history_params = malloc(2 * sizeof(ToolParameter));
    if (get_history_params == NULL) return -1;
    
    get_history_params[0] = (ToolParameter){
        .name = safe_strdup("days_back"),
        .type = safe_strdup("integer"),
        .description = safe_strdup("Number of days to look back (0 for all history)"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    get_history_params[1] = (ToolParameter){
        .name = safe_strdup("max_messages"),
        .type = safe_strdup("integer"),
        .description = safe_strdup("Maximum number of messages to retrieve"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    registry->functions[current_count] = (ToolFunction){
        .name = safe_strdup("get_conversation_history"),
        .description = safe_strdup("Retrieve extended conversation history from the vector database"),
        .parameters = get_history_params,
        .parameter_count = 2
    };
    
    // 2. Register search_conversation tool
    ToolParameter *search_params = malloc(2 * sizeof(ToolParameter));
    if (search_params == NULL) return -1;
    
    search_params[0] = (ToolParameter){
        .name = safe_strdup("query"),
        .type = safe_strdup("string"),
        .description = safe_strdup("The search query"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 1
    };
    
    search_params[1] = (ToolParameter){
        .name = safe_strdup("max_results"),
        .type = safe_strdup("integer"),
        .description = safe_strdup("Maximum number of results to return"),
        .enum_values = NULL,
        .enum_count = 0,
        .required = 0
    };
    
    registry->functions[current_count + 1] = (ToolFunction){
        .name = safe_strdup("search_conversation"),
        .description = safe_strdup("Search conversation history for relevant messages"),
        .parameters = search_params,
        .parameter_count = 2
    };
    
    registry->function_count = current_count + 2;
    
    return 0;
}