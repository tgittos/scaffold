#include "ralph.h"
#include "env_loader.h"
#include "output_formatter.h"
#include "prompt_loader.h"
#include "shell_tool.h"
#include "debug_output.h"
#include "api_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Forward declaration for Anthropic tools JSON generator
char* generate_anthropic_tools_json(const ToolRegistry *registry);
// Forward declaration for Anthropic tool call parser
int parse_anthropic_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count);

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
    return build_json_payload_common(model, system_prompt, conversation, user_message,
                                    max_tokens_param, max_tokens, tools,
                                    format_openai_message, 0);
}

// Helper function to build Anthropic-specific JSON payload
char* ralph_build_anthropic_json_payload(const char* model, const char* system_prompt,
                                        const ConversationHistory* conversation,
                                        const char* user_message, int max_tokens,
                                        const ToolRegistry* tools) {
    return build_json_payload_common(model, system_prompt, conversation, user_message,
                                    "max_tokens", max_tokens, tools,
                                    format_anthropic_message, 1);
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
    
    // Load environment variables from .env file (optional)
    if (load_env_file(".env") != 0) {
        // .env file is optional, just print debug message
        debug_printf("No .env file found, using environment variables or defaults\n");
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
    const char *api_key = NULL;
    if (strstr(session->config.api_url, "api.anthropic.com") != NULL) {
        api_key = getenv("ANTHROPIC_API_KEY");
    } else {
        api_key = getenv("OPENAI_API_KEY");
    }
    
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
    
    // Determine API type and correct parameter name for max tokens
    if (strstr(session->config.api_url, "api.openai.com") != NULL) {
        session->config.api_type = API_TYPE_OPENAI;
        session->config.max_tokens_param = "max_completion_tokens";
    } else if (strstr(session->config.api_url, "api.anthropic.com") != NULL) {
        session->config.api_type = API_TYPE_ANTHROPIC;
        session->config.max_tokens_param = "max_tokens";
    } else {
        session->config.api_type = API_TYPE_LOCAL;
        session->config.max_tokens_param = "max_tokens";
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
    
    // Note: User message is already saved by caller
    (void)user_message; // Acknowledge parameter usage
    
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
    char* follow_up_data = NULL;
    if (session->config.api_type == API_TYPE_ANTHROPIC) {
        follow_up_data = ralph_build_anthropic_json_payload(session->config.model, session->config.system_prompt,
                                                          &session->conversation, "", max_tokens, &session->tools);
    } else {
        follow_up_data = ralph_build_json_payload(session->config.model, session->config.system_prompt, 
                                                &session->conversation, "", 
                                                session->config.max_tokens_param, max_tokens, &session->tools);
    }
    // Tool execution was successful - we'll return success regardless of follow-up API status
    int result = 0;
    
    if (follow_up_data != NULL) {
        debug_printf("Follow-up POST data: %s\n", follow_up_data);
        debug_printf("Making follow-up request with tool results...\n");
        
        if (http_post_with_headers(session->config.api_url, follow_up_data, headers, &follow_up_response) == 0) {
            // Parse follow-up response based on API type
            ParsedResponse follow_up_parsed;
            int parse_result;
            if (session->config.api_type == API_TYPE_ANTHROPIC) {
                parse_result = parse_anthropic_response(follow_up_response.data, &follow_up_parsed);
            } else {
                parse_result = parse_api_response(follow_up_response.data, &follow_up_parsed);
            }
            
            if (parse_result == 0) {
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
    char* post_data = NULL;
    if (session->config.api_type == API_TYPE_ANTHROPIC) {
        post_data = ralph_build_anthropic_json_payload(session->config.model, session->config.system_prompt,
                                                      &session->conversation, user_message, -1, &session->tools);
    } else {
        post_data = ralph_build_json_payload(session->config.model, session->config.system_prompt, 
                                           &session->conversation, user_message, 
                                           session->config.max_tokens_param, -1, &session->tools);
    }
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
    if (session->config.api_type == API_TYPE_ANTHROPIC) {
        post_data = ralph_build_anthropic_json_payload(session->config.model, session->config.system_prompt,
                                                      &session->conversation, user_message, max_tokens, &session->tools);
    } else {
        post_data = ralph_build_json_payload(session->config.model, session->config.system_prompt, 
                                           &session->conversation, user_message, 
                                           session->config.max_tokens_param, max_tokens, &session->tools);
    }
    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build JSON payload\n");
        return -1;
    }
    
    // Setup authorization headers
    char auth_header[512];
    char anthropic_version[128] = "anthropic-version: 2023-06-01";
    char content_type[64] = "Content-Type: application/json";
    const char *headers[4] = {NULL, NULL, NULL, NULL};
    int header_count = 0;
    
    if (session->config.api_key != NULL) {
        if (session->config.api_type == API_TYPE_ANTHROPIC) {
            // Anthropic uses x-api-key header
            int ret = snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", session->config.api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                fprintf(stderr, "Error: Authorization header too long\n");
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
            headers[header_count++] = anthropic_version;
            headers[header_count++] = content_type;
        } else {
            // OpenAI and local servers use Bearer token
            int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", session->config.api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                fprintf(stderr, "Error: Authorization header too long\n");
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
        }
    }
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    debug_printf("Making API request to %s\n", session->config.api_url);
    debug_printf("POST data: %s\n\n", post_data);
    
    struct HTTPResponse response = {0};
    int result = -1;
    
    if (http_post_with_headers(session->config.api_url, post_data, headers, &response) == 0) {
        debug_printf("Got API response: %s\n", response.data);
        // Parse the response based on API type
        ParsedResponse parsed_response;
        int parse_result;
        if (session->config.api_type == API_TYPE_ANTHROPIC) {
            parse_result = parse_anthropic_response(response.data, &parsed_response);
        } else {
            parse_result = parse_api_response(response.data, &parsed_response);
        }
        
        if (parse_result != 0) {
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
        
        // Try to parse tool calls from raw JSON response
        ToolCall *raw_tool_calls = NULL;
        int raw_call_count = 0;
        int tool_parse_result;
        
        if (session->config.api_type == API_TYPE_ANTHROPIC) {
            tool_parse_result = parse_anthropic_tool_calls(response.data, &raw_tool_calls, &raw_call_count);
        } else {
            tool_parse_result = parse_tool_calls(response.data, &raw_tool_calls, &raw_call_count);
        }
        
        if (tool_parse_result == 0 && raw_call_count > 0) {
            debug_printf("Found %d tool calls in raw response\n", raw_call_count);
            // For tool calls, we need to save messages in the right order
            // First save user message, then assistant message, then execute tools
            
            // Save user message first
            if (append_conversation_message(&session->conversation, "user", user_message) != 0) {
                fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
            }
            
            // Then save assistant message with tool calls
            const char* content_to_save;
            if (session->config.api_type == API_TYPE_ANTHROPIC) {
                // Use a special format to preserve Anthropic tool_use blocks
                content_to_save = response.data;
            } else {
                content_to_save = parsed_response.response_content;
            }
            
            if (content_to_save != NULL) {
                if (append_conversation_message(&session->conversation, "assistant", content_to_save) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }
            }
            
            result = ralph_execute_tool_workflow(session, raw_tool_calls, raw_call_count, user_message, max_tokens, headers);
            cleanup_tool_calls(raw_tool_calls, raw_call_count);
        } else {
            debug_printf("No tool calls found in raw response (result: %d, count: %d)\n", tool_parse_result, raw_call_count);
            // Try to parse tool calls from message content (LM Studio format)
            ToolCall *tool_calls = NULL;
            int call_count = 0;
            if (message_content != NULL && parse_tool_calls(message_content, &tool_calls, &call_count) == 0 && call_count > 0) {
                // Save user message first for LM Studio format too
                if (append_conversation_message(&session->conversation, "user", user_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
                }
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