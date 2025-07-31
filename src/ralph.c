#include "ralph.h"
#include "env_loader.h"
#include "output_formatter.h"
#include "prompt_loader.h"
#include "shell_tool.h"
#include "debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Helper function to escape JSON strings
char* ralph_escape_json_string(const char* str) {
    if (str == NULL) return NULL;
    
    size_t len = strlen(str);
    // Worst case: every character needs escaping, plus quotes and null terminator
    char* escaped = malloc(len * 2 + 3);
    if (escaped == NULL) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        switch (str[i]) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            default:
                escaped[j++] = str[i];
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

// Helper function to build JSON payload with conversation history and tools
char* ralph_build_json_payload(const char* model, const char* system_prompt, 
                              const ConversationHistory* conversation, 
                              const char* user_message, const char* max_tokens_param, 
                              int max_tokens, const ToolRegistry* tools) {
    // Calculate required buffer size
    size_t base_size = 200; // Base JSON structure
    size_t model_len = strlen(model);
    size_t user_msg_len = strlen(user_message) * 2 + 50; // *2 for escaping, +50 for JSON structure
    size_t system_len = (system_prompt != NULL) ? strlen(system_prompt) * 2 + 50 : 0;
    size_t history_len = 0;
    
    // Calculate space needed for conversation history
    for (int i = 0; i < conversation->count; i++) {
        history_len += strlen(conversation->messages[i].role) + 
                      strlen(conversation->messages[i].content) * 2 + 50;
    }
    
    // Account for tools JSON size
    size_t tools_len = 0;
    if (tools != NULL && tools->function_count > 0) {
        tools_len = tools->function_count * 500; // Rough estimate per tool
    }
    
    size_t total_size = base_size + model_len + user_msg_len + system_len + history_len + tools_len + 200;
    char* json = malloc(total_size);
    if (json == NULL) return NULL;
    
    // Start building JSON
    strcpy(json, "{\"model\": \"");
    strcat(json, model);
    strcat(json, "\", \"messages\": [");
    
    // Determine if we need to add user message
    int will_add_user_message = (user_message != NULL && strlen(user_message) > 0);
    int message_count = 0; // Track how many messages we've added
    
    // Add system prompt if available
    if (system_prompt != NULL) {
        char* escaped_system = ralph_escape_json_string(system_prompt);
        if (escaped_system != NULL) {
            strcat(json, "{\"role\": \"system\", \"content\": \"");
            strcat(json, escaped_system);
            strcat(json, "\"}");
            free(escaped_system);
            message_count++;
        }
    }
    
    // Add conversation history
    for (int i = 0; i < conversation->count; i++) {
        char* escaped_content = ralph_escape_json_string(conversation->messages[i].content);
        if (escaped_content != NULL) {
            if (message_count > 0) {
                strcat(json, ", ");
            }
            strcat(json, "{\"role\": \"");
            strcat(json, conversation->messages[i].role);
            strcat(json, "\", \"content\": \"");
            strcat(json, escaped_content);
            strcat(json, "\"");
            
            // Add tool metadata for tool messages
            if (strcmp(conversation->messages[i].role, "tool") == 0 && 
                conversation->messages[i].tool_call_id != NULL) {
                strcat(json, ", \"tool_call_id\": \"");
                strcat(json, conversation->messages[i].tool_call_id);
                strcat(json, "\"");
            }
            
            strcat(json, "}");
            free(escaped_content);
            message_count++;
        }
    }
    
    // Add current user message (only if not empty)
    if (will_add_user_message) {
        char* escaped_user_msg = ralph_escape_json_string(user_message);
        if (escaped_user_msg != NULL) {
            if (message_count > 0) {
                strcat(json, ", ");
            }
            strcat(json, "{\"role\": \"user\", \"content\": \"");
            strcat(json, escaped_user_msg);
            strcat(json, "\"}");
            free(escaped_user_msg);
            message_count++;
        }
    }
    
    // Close messages array and add max_tokens parameter
    strcat(json, "], \"");
    strcat(json, max_tokens_param);
    strcat(json, "\": ");
    
    char tokens_str[20];
    snprintf(tokens_str, sizeof(tokens_str), "%d", max_tokens);
    strcat(json, tokens_str);
    
    // Add tools if available
    if (tools != NULL && tools->function_count > 0) {
        char* tools_json = generate_tools_json(tools);
        if (tools_json != NULL) {
            strcat(json, ", \"tools\": ");
            strcat(json, tools_json);
            free(tools_json);
        }
    }
    
    strcat(json, "}");
    
    return json;
}

int ralph_init_session(RalphSession* session) {
    if (session == NULL) return -1;
    
    // Initialize configuration structure
    memset(&session->config, 0, sizeof(RalphConfig));
    
    // Load conversation history
    if (load_conversation_history(&session->conversation) != 0) {
        fprintf(stderr, "Error: Failed to load conversation history\n");
        return -1;
    }
    
    // Initialize tool registry and register all built-in tools
    init_tool_registry(&session->tools);
    if (register_builtin_tools(&session->tools) != 0) {
        fprintf(stderr, "Warning: Failed to register built-in tools\n");
    }
    
    // Optionally load custom tools from configuration file (non-critical)
    if (load_tools_config(&session->tools, "tools.json") != 0) {
        fprintf(stderr, "Warning: Failed to load custom tools configuration\n");
    }
    
    return 0;
}

void ralph_cleanup_session(RalphSession* session) {
    if (session == NULL) return;
    
    // Cleanup configuration
    free(session->config.api_url);
    free(session->config.model);
    free(session->config.api_key);
    cleanup_system_prompt(&session->config.system_prompt);
    
    // Cleanup conversation and tools
    cleanup_conversation_history(&session->conversation);
    cleanup_tool_registry(&session->tools);
    
    // Reset configuration
    memset(&session->config, 0, sizeof(RalphConfig));
}

int ralph_load_config(RalphSession* session) {
    if (session == NULL) return -1;
    
    // Load environment variables from .env file
    if (load_env_file(".env") != 0) {
        fprintf(stderr, "Error: Failed to load .env file\n");
        return -1;
    }
    
    // Try to load system prompt from PROMPT.md (optional)
    load_system_prompt(&session->config.system_prompt);
    
    // Get API URL from environment variable, default to OpenAI
    const char *url = getenv("API_URL");
    if (url == NULL) {
        url = "https://api.openai.com/v1/chat/completions";
    }
    session->config.api_url = strdup(url);
    if (session->config.api_url == NULL) return -1;
    
    // Get model from environment variable, default to o4-mini
    const char *model = getenv("MODEL");
    if (model == NULL) {
        model = "o4-mini-2025-04-16";
    }
    session->config.model = strdup(model);
    if (session->config.model == NULL) return -1;
    
    // Get API key (optional for local servers)
    const char *api_key = getenv("OPENAI_API_KEY");
    if (api_key != NULL) {
        session->config.api_key = strdup(api_key);
        if (session->config.api_key == NULL) return -1;
    }
    
    // Get context window size from environment variable
    const char *context_window_str = getenv("CONTEXT_WINDOW");
    session->config.context_window = 8192;  // Default for many models
    if (context_window_str != NULL) {
        int parsed_window = atoi(context_window_str);
        if (parsed_window > 0) {
            session->config.context_window = parsed_window;
        }
    }
    
    // Get max response tokens from environment variable (optional override)
    const char *max_tokens_str = getenv("MAX_TOKENS");
    session->config.max_tokens = -1;  // Will be calculated later if not set
    if (max_tokens_str != NULL) {
        int parsed_tokens = atoi(max_tokens_str);
        if (parsed_tokens > 0) {
            session->config.max_tokens = parsed_tokens;
        }
    }
    
    // Determine the correct parameter name for max tokens
    // OpenAI uses "max_completion_tokens" for newer models, while local servers typically use "max_tokens"
    session->config.max_tokens_param = "max_tokens";
    if (strstr(session->config.api_url, "api.openai.com") != NULL) {
        session->config.max_tokens_param = "max_completion_tokens";
    }
    
    return 0;
}

// Common tool calling workflow that handles both OpenAI and LM Studio formats
int ralph_execute_tool_workflow(RalphSession* session, ToolCall* tool_calls, int call_count, 
                               const char* user_message, int max_tokens, const char** headers) {
    if (session == NULL || tool_calls == NULL || call_count <= 0) {
        return -1;
    }
    
    debug_printf("Executing %d tool call(s)...\n", call_count);
    
    // Save user message to conversation first
    if (append_conversation_message(&session->conversation, "user", user_message) != 0) {
        fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
    }
    
    // Execute tool calls
    ToolResult *results = malloc(call_count * sizeof(ToolResult));
    if (results == NULL) {
        return -1;
    }
    
    for (int i = 0; i < call_count; i++) {
        if (execute_tool_call(&session->tools, &tool_calls[i], &results[i]) != 0) {
            fprintf(stderr, "Warning: Failed to execute tool call %s\n", tool_calls[i].name);
            results[i].tool_call_id = strdup(tool_calls[i].id);
            results[i].result = strdup("Tool execution failed");
            results[i].success = 0;
            
            // Handle strdup failures
            if (results[i].tool_call_id == NULL) {
                results[i].tool_call_id = strdup("unknown");
            }
            if (results[i].result == NULL) {
                results[i].result = strdup("Memory allocation failed");
            }
        } else {
            debug_printf("Executed tool: %s (ID: %s)\n", tool_calls[i].name, tool_calls[i].id);
        }
    }
    
    // Add tool result messages to conversation
    for (int i = 0; i < call_count; i++) {
        if (append_tool_message(&session->conversation, results[i].result, tool_calls[i].id, tool_calls[i].name) != 0) {
            fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
        }
    }
    
    // Make follow-up request with tool results
    struct HTTPResponse follow_up_response = {0};
    
    debug_printf("Building follow-up JSON payload...\n");
    // Build new JSON payload with conversation including tool results
    char* follow_up_data = ralph_build_json_payload(session->config.model, session->config.system_prompt, 
                                                   &session->conversation, "", 
                                                   session->config.max_tokens_param, max_tokens, &session->tools);
    // Tool execution was successful - we'll return success regardless of follow-up API status
    int result = 0;
    
    if (follow_up_data != NULL) {
        debug_printf("Follow-up POST data: %s\n", follow_up_data);
        debug_printf("Making follow-up request with tool results...\n");
        
        if (http_post_with_headers(session->config.api_url, follow_up_data, headers, &follow_up_response) == 0) {
            // Parse follow-up response
            ParsedResponse follow_up_parsed;
            if (parse_api_response(follow_up_response.data, &follow_up_parsed) == 0) {
                // Display the final response
                print_formatted_response(&follow_up_parsed);
                
                // Save final assistant response to conversation
                const char* final_content = follow_up_parsed.response_content ? 
                                           follow_up_parsed.response_content : 
                                           follow_up_parsed.thinking_content;
                if (final_content != NULL) {
                    if (append_conversation_message(&session->conversation, "assistant", final_content) != 0) {
                        fprintf(stderr, "Warning: Failed to save final assistant response to conversation history\n");
                    }
                }
                
                cleanup_parsed_response(&follow_up_parsed);
            } else {
                fprintf(stderr, "Error: Failed to parse follow-up API response\n");
                printf("%s\n", follow_up_response.data);
            }
        } else {
            fprintf(stderr, "Follow-up API request failed - tool execution was still successful\n");
        }
        
        cleanup_response(&follow_up_response);
        free(follow_up_data);
    }
    
    cleanup_tool_results(results, call_count);
    return result;
}

int ralph_process_message(RalphSession* session, const char* user_message) {
    if (session == NULL || user_message == NULL) return -1;
    
    // Build JSON payload first to get accurate size
    char* post_data = ralph_build_json_payload(session->config.model, session->config.system_prompt, 
                                             &session->conversation, user_message, 
                                             session->config.max_tokens_param, -1, &session->tools);
    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build JSON payload\n");
        return -1;
    }
    
    // Estimate prompt tokens based on actual payload size (rough approximation: ~4 chars per token)
    int estimated_prompt_tokens = (int)(strlen(post_data) / 4) + 50; // +50 for JSON overhead
    
    // Calculate max response tokens if not explicitly set
    int max_tokens = session->config.max_tokens;
    if (max_tokens == -1) {
        max_tokens = session->config.context_window - estimated_prompt_tokens - 100; // -100 for safety buffer
        if (max_tokens < 100) {
            max_tokens = 100; // Minimum reasonable response length
        }
    }
    
    // Rebuild payload with correct max_tokens
    free(post_data);
    post_data = ralph_build_json_payload(session->config.model, session->config.system_prompt, 
                                       &session->conversation, user_message, 
                                       session->config.max_tokens_param, max_tokens, &session->tools);
    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build JSON payload\n");
        return -1;
    }
    
    // Setup authorization headers if API key is available
    char auth_header[512];
    const char *headers[2] = {NULL, NULL};
    
    if (session->config.api_key != NULL) {
        int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", session->config.api_key);
        if (ret < 0 || ret >= (int)sizeof(auth_header)) {
            fprintf(stderr, "Error: Authorization header too long\n");
            free(post_data);
            return -1;
        }
        headers[0] = auth_header;
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    debug_printf("Making API request to %s\n", session->config.api_url);
    debug_printf("POST data: %s\n\n", post_data);
    
    struct HTTPResponse response = {0};
    int result = -1;
    
    if (http_post_with_headers(session->config.api_url, post_data, headers, &response) == 0) {
        // Parse the normal response first to get message content
        ParsedResponse parsed_response;
        if (parse_api_response(response.data, &parsed_response) != 0) {
            fprintf(stderr, "Error: Failed to parse API response\n");
            printf("%s\n", response.data);  // Fallback to raw output
            cleanup_response(&response);
            free(post_data);
            curl_global_cleanup();
            return -1;
        }
        
        const char* message_content = parsed_response.response_content ? 
                                     parsed_response.response_content : 
                                     parsed_response.thinking_content;
        
        // Try to parse tool calls from raw JSON response (OpenAI format)
        ToolCall *raw_tool_calls = NULL;
        int raw_call_count = 0;
        if (parse_tool_calls(response.data, &raw_tool_calls, &raw_call_count) == 0 && raw_call_count > 0) {
            // Save assistant message with tool calls to conversation
            if (parsed_response.response_content != NULL) {
                if (append_conversation_message(&session->conversation, "assistant", parsed_response.response_content) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }
            }
            
            result = ralph_execute_tool_workflow(session, raw_tool_calls, raw_call_count, user_message, max_tokens, headers);
            cleanup_tool_calls(raw_tool_calls, raw_call_count);
        } else {
            // Try to parse tool calls from message content (LM Studio format)
            ToolCall *tool_calls = NULL;
            int call_count = 0;
            if (message_content != NULL && parse_tool_calls(message_content, &tool_calls, &call_count) == 0 && call_count > 0) {
                result = ralph_execute_tool_workflow(session, tool_calls, call_count, user_message, max_tokens, headers);
                cleanup_tool_calls(tool_calls, call_count);
            } else {
                // No tool calls - normal response handling
                print_formatted_response(&parsed_response);
                
                // Save user message
                if (append_conversation_message(&session->conversation, "user", user_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
                }
                
                // Use response_content for the assistant's message (this is the main content without thinking)
                const char* assistant_content = parsed_response.response_content ? 
                                               parsed_response.response_content : 
                                               parsed_response.thinking_content;
                if (assistant_content != NULL) {
                    if (append_conversation_message(&session->conversation, "assistant", assistant_content) != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                }
                result = 0;
            }
        }
        
        cleanup_parsed_response(&parsed_response);
    } else {
        fprintf(stderr, "API request failed\n");
    }
    
    cleanup_response(&response);
    free(post_data);
    curl_global_cleanup();
    return result;
}