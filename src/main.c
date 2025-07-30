#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "http_client.h"
#include "env_loader.h"
#include "output_formatter.h"
#include "prompt_loader.h"
#include "conversation_tracker.h"
#include "tools_system.h"
#include "shell_tool.h"

// Helper function to escape JSON strings
static char* escape_json_string(const char* str) {
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
static char* build_json_payload(const char* model, const char* system_prompt, 
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
    
    // Add system prompt if available
    if (system_prompt != NULL) {
        char* escaped_system = escape_json_string(system_prompt);
        if (escaped_system != NULL) {
            strcat(json, "{\"role\": \"system\", \"content\": \"");
            strcat(json, escaped_system);
            strcat(json, "\"}");
            free(escaped_system);
            
            if (conversation->count > 0) {
                strcat(json, ", ");
            }
        }
    }
    
    // Add conversation history
    for (int i = 0; i < conversation->count; i++) {
        char* escaped_content = escape_json_string(conversation->messages[i].content);
        if (escaped_content != NULL) {
            if (i > 0 || system_prompt != NULL) {
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
        }
    }
    
    // Add current user message
    char* escaped_user_msg = escape_json_string(user_message);
    if (escaped_user_msg != NULL) {
        if (conversation->count > 0 || system_prompt != NULL) {
            strcat(json, ", ");
        }
        strcat(json, "{\"role\": \"user\", \"content\": \"");
        strcat(json, escaped_user_msg);
        strcat(json, "\"}");
        free(escaped_user_msg);
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

int main(int argc, char *argv[])
{
    // Load environment variables from .env file
    if (load_env_file(".env") != 0) {
        fprintf(stderr, "Error: Failed to load .env file\n");
        return EXIT_FAILURE;
    }
    
    // Load conversation history
    ConversationHistory conversation;
    if (load_conversation_history(&conversation) != 0) {
        fprintf(stderr, "Error: Failed to load conversation history\n");
        return EXIT_FAILURE;
    }
    
    // Initialize tool registry and register all built-in tools
    ToolRegistry tools;
    init_tool_registry(&tools);
    if (register_builtin_tools(&tools) != 0) {
        fprintf(stderr, "Warning: Failed to register built-in tools\n");
    }
    
    // Optionally load custom tools from configuration file (non-critical)
    if (load_tools_config(&tools, "tools.json") != 0) {
        fprintf(stderr, "Warning: Failed to load custom tools configuration\n");
    }
    
    if (argc != 2) {
        fprintf(stderr, "Usage: %s \"<message>\"\n", argv[0]);
        fprintf(stderr, "Example: %s \"Hello, how are you?\"\n", argv[0]);
        cleanup_conversation_history(&conversation);
        cleanup_tool_registry(&tools);
        return EXIT_FAILURE;
    }
    
    struct HTTPResponse response = {0};
    const char *user_message = argv[1];
    char *system_prompt = NULL;
    
    // Try to load system prompt from PROMPT.md (optional)
    load_system_prompt(&system_prompt);
    
    // Get API URL from environment variable, default to OpenAI
    const char *url = getenv("API_URL");
    if (url == NULL) {
        url = "https://api.openai.com/v1/chat/completions";
    }
    
    // Get model from environment variable, default to gpt-3.5-turbo
    const char *model = getenv("MODEL");
    if (model == NULL) {
        model = "o4-mini-2025-04-16";
    }
    
    // Get context window size from environment variable
    const char *context_window_str = getenv("CONTEXT_WINDOW");
    int context_window = 8192;  // Default for many models
    if (context_window_str != NULL) {
        int parsed_window = atoi(context_window_str);
        if (parsed_window > 0) {
            context_window = parsed_window;
        }
    }
    
    // Get max response tokens from environment variable (optional override)
    const char *max_tokens_str = getenv("MAX_TOKENS");
    int max_tokens = -1;  // Will be calculated later if not set
    if (max_tokens_str != NULL) {
        int parsed_tokens = atoi(max_tokens_str);
        if (parsed_tokens > 0) {
            max_tokens = parsed_tokens;
        }
    }
    
    // Estimate prompt tokens (rough approximation: ~4 chars per token)
    int estimated_prompt_tokens = (int)(strlen(user_message) / 4) + 20; // +20 for system overhead
    
    // Calculate max response tokens if not explicitly set
    if (max_tokens == -1) {
        max_tokens = context_window - estimated_prompt_tokens - 50; // -50 for safety buffer
        if (max_tokens < 100) {
            max_tokens = 100; // Minimum reasonable response length
        }
    }
    
    // Determine the correct parameter name for max tokens
    // OpenAI uses "max_completion_tokens" for newer models, while local servers typically use "max_tokens"
    const char *max_tokens_param = "max_tokens";
    if (strstr(url, "api.openai.com") != NULL) {
        max_tokens_param = "max_completion_tokens";
    }
    
    // Build JSON payload with conversation history and tools
    char* post_data = build_json_payload(model, system_prompt, &conversation, 
                                        user_message, max_tokens_param, max_tokens, &tools);
    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build JSON payload\n");
        cleanup_system_prompt(&system_prompt);
        cleanup_conversation_history(&conversation);
        cleanup_tool_registry(&tools);
        return EXIT_FAILURE;
    }
    
    // Setup authorization - optional for local servers
    const char *api_key = getenv("OPENAI_API_KEY");
    char auth_header[512];
    const char *headers[2] = {NULL, NULL};
    
    if (api_key != NULL) {
        int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
        if (ret < 0 || ret >= (int)sizeof(auth_header)) {
            fprintf(stderr, "Error: Authorization header too long\n");
            cleanup_system_prompt(&system_prompt);
            cleanup_conversation_history(&conversation);
            cleanup_tool_registry(&tools);
            if (post_data != NULL) {
                free(post_data);
                post_data = NULL;
            }
            return EXIT_FAILURE;
        }
        headers[0] = auth_header;
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    fprintf(stderr, "Making API request to %s\n", url);
    fprintf(stderr, "POST data: %s\n\n", post_data);
    
    if (http_post_with_headers(url, post_data, headers, &response) == 0) {
        // Parse the normal response first to get message content
        ParsedResponse parsed_response;
        if (parse_api_response(response.data, &parsed_response) != 0) {
            fprintf(stderr, "Error: Failed to parse API response\n");
            printf("%s\n", response.data);  // Fallback to raw output
            cleanup_response(&response);
            cleanup_system_prompt(&system_prompt);
            cleanup_conversation_history(&conversation);
            cleanup_tool_registry(&tools);
            if (post_data != NULL) {
                free(post_data);
                post_data = NULL;
            }
            curl_global_cleanup();
            return EXIT_FAILURE;
        }
        
        // Parse response for tool calls from message content
        ToolCall *tool_calls = NULL;
        int call_count = 0;
        
        const char* message_content = parsed_response.response_content ? 
                                     parsed_response.response_content : 
                                     parsed_response.thinking_content;
        
        // Check if this is a direct tool call response (from raw JSON)
        ToolCall *raw_tool_calls = NULL;
        int raw_call_count = 0;
        if (parse_tool_calls(response.data, &raw_tool_calls, &raw_call_count) == 0 && raw_call_count > 0) {
            // We have tool calls from raw response to execute
            fprintf(stderr, "Executing %d tool call(s) from API response...\n", raw_call_count);
            
            // Save user message to conversation first
            if (append_conversation_message(&conversation, "user", user_message) != 0) {
                fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
            }
            
            // Save assistant message with tool calls to conversation
            if (parsed_response.response_content != NULL) {
                if (append_conversation_message(&conversation, "assistant", parsed_response.response_content) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }
            }
            
            // Execute tool calls
            ToolResult *results = malloc(raw_call_count * sizeof(ToolResult));
            if (results != NULL) {
                for (int i = 0; i < raw_call_count; i++) {
                    if (execute_tool_call(&tools, &raw_tool_calls[i], &results[i]) != 0) {
                        fprintf(stderr, "Warning: Failed to execute tool call %s\n", raw_tool_calls[i].name);
                        results[i].tool_call_id = strdup(raw_tool_calls[i].id);
                        results[i].result = strdup("Tool execution failed");
                        results[i].success = 0;
                    } else {
                        fprintf(stderr, "Executed tool: %s (ID: %s)\n", raw_tool_calls[i].name, raw_tool_calls[i].id);
                    }
                }
                
                // Add tool result messages to conversation
                for (int i = 0; i < raw_call_count; i++) {
                    if (append_tool_message(&conversation, results[i].result, raw_tool_calls[i].id, raw_tool_calls[i].name) != 0) {
                        fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
                    }
                }
                
                // Make follow-up request with tool results
                struct HTTPResponse follow_up_response = {0};
                
                // Build new JSON payload with conversation including tool results
                char* follow_up_data = build_json_payload(model, system_prompt, &conversation, 
                                                         "", max_tokens_param, max_tokens, NULL);
                if (follow_up_data != NULL) {
                    fprintf(stderr, "Making follow-up request with tool results...\n");
                    
                    if (http_post_with_headers(url, follow_up_data, headers, &follow_up_response) == 0) {
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
                                if (append_conversation_message(&conversation, "assistant", final_content) != 0) {
                                    fprintf(stderr, "Warning: Failed to save final assistant response to conversation history\n");
                                }
                            }
                            
                            cleanup_parsed_response(&follow_up_parsed);
                        } else {
                            fprintf(stderr, "Error: Failed to parse follow-up API response\n");
                            printf("%s\n", follow_up_response.data);
                        }
                        
                        cleanup_response(&follow_up_response);
                    } else {
                        fprintf(stderr, "Follow-up API request failed\n");
                    }
                    
                    free(follow_up_data);
                }
                
                cleanup_tool_results(results, raw_call_count);
            }
            
            cleanup_tool_calls(raw_tool_calls, raw_call_count);
        } else if (message_content != NULL && parse_tool_calls(message_content, &tool_calls, &call_count) == 0 && call_count > 0) {
            // Fallback: tool calls found in message content (for LM Studio format)
            fprintf(stderr, "Executing %d tool call(s) from message content...\n", call_count);
            
            // Save user message to conversation first
            if (append_conversation_message(&conversation, "user", user_message) != 0) {
                fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
            }
            
            // Execute tool calls
            ToolResult *results = malloc(call_count * sizeof(ToolResult));
            if (results != NULL) {
                for (int i = 0; i < call_count; i++) {
                    if (execute_tool_call(&tools, &tool_calls[i], &results[i]) != 0) {
                        fprintf(stderr, "Warning: Failed to execute tool call %s\n", tool_calls[i].name);
                        results[i].tool_call_id = strdup(tool_calls[i].id);
                        results[i].result = strdup("Tool execution failed");
                        results[i].success = 0;
                    } else {
                        fprintf(stderr, "Executed tool: %s\n", tool_calls[i].name);
                    }
                }
                
                // For LM Studio, just show results (no follow-up needed)
                fprintf(stderr, "Tool execution completed.\n");
                
                cleanup_tool_results(results, call_count);
            }
            
            cleanup_tool_calls(tool_calls, call_count);
        } else {
            // No tool calls - normal response handling
            
            // Display the response and save to conversation
            print_formatted_response(&parsed_response);
            
            // Save user message
            if (append_conversation_message(&conversation, "user", user_message) != 0) {
                fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
            }
            
            // Use response_content for the assistant's message (this is the main content without thinking)
            const char* assistant_content = parsed_response.response_content ? 
                                           parsed_response.response_content : 
                                           parsed_response.thinking_content;
            if (assistant_content != NULL) {
                if (append_conversation_message(&conversation, "assistant", assistant_content) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }
            }
        }
        
        cleanup_parsed_response(&parsed_response);
    } else {
        fprintf(stderr, "API request failed\n");
        cleanup_response(&response);
        cleanup_system_prompt(&system_prompt);
        cleanup_conversation_history(&conversation);
        cleanup_tool_registry(&tools);
        if (post_data != NULL) {
            free(post_data);
            post_data = NULL;
        }
        curl_global_cleanup();
        return EXIT_FAILURE;
    }
    
    cleanup_response(&response);
    cleanup_system_prompt(&system_prompt);
    cleanup_conversation_history(&conversation);
    cleanup_tool_registry(&tools);
    if (post_data != NULL) {
        free(post_data);
        post_data = NULL;
    }
    curl_global_cleanup();
    return EXIT_SUCCESS;
}