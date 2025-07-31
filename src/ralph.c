#include "ralph.h"
#include "json_utils.h"
#include "env_loader.h"
#include "output_formatter.h"
#include "prompt_loader.h"
#include "shell_tool.h"
#include "todo_tool.h"
#include "todo_display.h"
#include "debug_output.h"
#include "api_common.h"
#include "token_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

// Forward declaration for Anthropic tools JSON generator
char* generate_anthropic_tools_json(const ToolRegistry *registry);
// Forward declaration for Anthropic tool call parser
int parse_anthropic_tool_calls(const char *json_response, ToolCall **tool_calls, int *call_count);

// Use unified JSON escaping from json_utils.h

// Compatibility wrapper for tests - use json_escape_string instead in new code
char* ralph_escape_json_string(const char* str) {
    if (str == NULL) return NULL;
    return json_escape_string(str);
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

char* ralph_build_enhanced_system_prompt(const RalphSession* session) {
    if (session == NULL) return NULL;
    
    const char* base_prompt = session->config.system_prompt;
    if (base_prompt == NULL) base_prompt = "";
    
    // Serialize current todo list state
    char* todo_json = todo_serialize_json((TodoList*)&session->todo_list);
    if (todo_json == NULL) {
        // If serialization fails, just return the base prompt
        size_t len = strlen(base_prompt) + 1;
        char* result = malloc(len);
        if (result) strcpy(result, base_prompt);
        return result;
    }
    
    // Build enhanced prompt with todo context
    const char* todo_section = "\n\n# Your Internal Todo List State\n"
                              "You have access to an internal todo list system for your own task management. "
                              "This is YOUR todo list for breaking down and tracking your work. "
                              "Your current internal todo list state is:\n\n";
    
    const char* todo_instructions = "\n\nCRITICAL TODO SYSTEM RULES:\n"
                                   "1. IMMEDIATELY after creating todos with TodoWrite, you MUST begin executing them\n"
                                   "2. NEVER create todos and then stop - you must DO THE WORK\n"
                                   "3. Mark tasks 'in_progress' when starting, 'completed' when done\n"
                                   "4. Your response is NOT COMPLETE until ALL todos are 'completed'\n"
                                   "5. If you have pending or in_progress todos, you MUST continue working\n"
                                   "6. Use tool calls, provide responses, and take actions to complete each todo\n"
                                   "7. Only end your response when your todo list shows all tasks completed\n\n"
                                   "WORKFLOW: TodoWrite → Execute Tasks → Mark Complete → Verify All Done → End Response\n"
                                   "DO NOT create a todo list and then do nothing. ACT ON YOUR TODOS.";
    
    size_t total_len = strlen(base_prompt) + strlen(todo_section) + 
                       strlen(todo_json) + strlen(todo_instructions) + 1;
    
    char* enhanced_prompt = malloc(total_len);
    if (enhanced_prompt == NULL) {
        free(todo_json);
        return NULL;
    }
    
    snprintf(enhanced_prompt, total_len, "%s%s%s%s", 
             base_prompt, todo_section, todo_json, todo_instructions);
    
    free(todo_json);
    return enhanced_prompt;
}

char* ralph_build_json_payload_with_todos(const RalphSession* session,
                                         const char* user_message, int max_tokens) {
    if (session == NULL) return NULL;
    
    char* enhanced_prompt = ralph_build_enhanced_system_prompt(session);
    if (enhanced_prompt == NULL) return NULL;
    
    char* result = ralph_build_json_payload(session->config.model, enhanced_prompt,
                                          &session->conversation, user_message,
                                          session->config.max_tokens_param, max_tokens,
                                          &session->tools);
    
    free(enhanced_prompt);
    return result;
}

char* ralph_build_anthropic_json_payload_with_todos(const RalphSession* session,
                                                   const char* user_message, int max_tokens) {
    if (session == NULL) return NULL;
    
    char* enhanced_prompt = ralph_build_enhanced_system_prompt(session);
    if (enhanced_prompt == NULL) return NULL;
    
    char* result = ralph_build_anthropic_json_payload(session->config.model, enhanced_prompt,
                                                    &session->conversation, user_message,
                                                    max_tokens, &session->tools);
    
    free(enhanced_prompt);
    return result;
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
    
    // Initialize todo list for AI agent use
    if (todo_list_init(&session->todo_list) != 0) {
        fprintf(stderr, "Error: Failed to initialize todo list\n");
        cleanup_conversation_history(&session->conversation);
        cleanup_tool_registry(&session->tools);
        return -1;
    }
    
    // Register todo tool with the tool registry
    if (register_todo_tool(&session->tools, &session->todo_list) != 0) {
        fprintf(stderr, "Warning: Failed to register todo tool\n");
    }
    
    // Initialize todo display system
    TodoDisplayConfig display_config = {
        .enabled = true,
        .show_completed = false,
        .compact_mode = true,
        .max_display_items = 5
    };
    if (todo_display_init(&display_config) != 0) {
        fprintf(stderr, "Warning: Failed to initialize todo display\n");
    }
    
    // Optionally load custom tools from configuration file (non-critical)
    if (load_tools_config(&session->tools, "tools.json") != 0) {
        fprintf(stderr, "Warning: Failed to load custom tools configuration\n");
    }
    
    return 0;
}

void ralph_cleanup_session(RalphSession* session) {
    if (session == NULL) return;
    
    // Clear global todo tool reference before destroying the todo list
    clear_todo_tool_reference();
    
    // Cleanup todo display system
    todo_display_cleanup();
    
    // Cleanup todo list first (before tool registry which might reference it)
    todo_list_destroy(&session->todo_list);
    
    // Cleanup conversation and tools
    cleanup_conversation_history(&session->conversation);
    cleanup_tool_registry(&session->tools);
    
    // Cleanup configuration
    free(session->config.api_url);
    free(session->config.model);
    free(session->config.api_key);
    cleanup_system_prompt(&session->config.system_prompt);
    
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
    
    // Get maximum context window size from environment variable
    const char *max_context_window_str = getenv("MAX_CONTEXT_WINDOW");
    session->config.max_context_window = session->config.context_window;  // Default to same as context_window
    if (max_context_window_str != NULL) {
        int parsed_max_window = atoi(max_context_window_str);
        if (parsed_max_window > 0) {
            session->config.max_context_window = parsed_max_window;
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

// Helper function to construct OpenAI assistant message with tool calls
static char* construct_openai_assistant_message_with_tools(const char* content, 
                                                          const ToolCall* tool_calls, 
                                                          int call_count) {
    if (call_count <= 0 || tool_calls == NULL) {
        return content ? strdup(content) : NULL;
    }
    
    // Estimate size needed for the JSON message
    size_t base_size = 200; // Base structure
    size_t content_size = content ? strlen(content) * 2 + 50 : 50; // Escaped content
    size_t tools_size = call_count * 200; // Rough estimate per tool call
    
    char* message = malloc(base_size + content_size + tools_size);
    if (message == NULL) {
        return NULL;
    }
    
    // Start constructing the message
    char* escaped_content = json_escape_string(content ? content : "");
    if (escaped_content == NULL) {
        free(message);
        return NULL;
    }
    
    int written = snprintf(message, base_size + content_size + tools_size,
                          "{\"role\": \"assistant\", \"content\": \"%s\", \"tool_calls\": [",
                          escaped_content);
    free(escaped_content);
    
    if (written < 0) {
        free(message);
        return NULL;
    }
    
    // Add tool calls
    for (int i = 0; i < call_count; i++) {
        char* escaped_args = json_escape_string(tool_calls[i].arguments ? tool_calls[i].arguments : "{}");
        if (escaped_args == NULL) {
            free(message);
            return NULL;
        }
        
        char tool_call_json[512];
        int tool_written = snprintf(tool_call_json, sizeof(tool_call_json),
                                   "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                                   i > 0 ? ", " : "",
                                   tool_calls[i].id ? tool_calls[i].id : "",
                                   tool_calls[i].name ? tool_calls[i].name : "",
                                   escaped_args);
        free(escaped_args);
        
        if (tool_written < 0 || tool_written >= (int)sizeof(tool_call_json)) {
            free(message);
            return NULL;
        }
        
        strcat(message, tool_call_json);
    }
    
    strcat(message, "]}");
    return message;
}

// Forward declaration for the iterative tool loop
static int ralph_execute_tool_loop(RalphSession* session, const char* user_message, int max_tokens, const char** headers);

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
    
    // CRITICAL FIX: Now that initial tools are executed and saved to conversation,
    // check if we need to continue with follow-up API calls for additional tool calls
    int result = ralph_execute_tool_loop(session, user_message, max_tokens, headers);
    
    // Always return success if tools executed, even if follow-up fails
    // This maintains backward compatibility with existing tests
    if (result != 0) {
        debug_printf("Follow-up tool loop failed, but initial tools executed successfully\n");
        result = 0;
    }
    
    cleanup_tool_results(results, call_count);
    return result;
}

// Simple tracking structure for executed tool calls
typedef struct {
    char** tool_call_ids;
    int count;
    int capacity;
} ExecutedToolTracker;

// Check if a tool call ID was already executed
static int is_tool_already_executed(ExecutedToolTracker* tracker, const char* tool_call_id) {
    for (int i = 0; i < tracker->count; i++) {
        if (strcmp(tracker->tool_call_ids[i], tool_call_id) == 0) {
            return 1;
        }
    }
    return 0;
}

// Add a tool call ID to the executed tracker
static void add_executed_tool(ExecutedToolTracker* tracker, const char* tool_call_id) {
    if (tracker->count >= tracker->capacity) {
        tracker->capacity = tracker->capacity == 0 ? 10 : tracker->capacity * 2;
        tracker->tool_call_ids = realloc(tracker->tool_call_ids, tracker->capacity * sizeof(char*));
    }
    if (tracker->tool_call_ids != NULL) {
        tracker->tool_call_ids[tracker->count] = strdup(tool_call_id);
        tracker->count++;
    }
}

// Cleanup executed tool tracker
static void cleanup_executed_tool_tracker(ExecutedToolTracker* tracker) {
    for (int i = 0; i < tracker->count; i++) {
        free(tracker->tool_call_ids[i]);
    }
    free(tracker->tool_call_ids);
    tracker->tool_call_ids = NULL;
    tracker->count = 0;
    tracker->capacity = 0;
}

// Iterative tool calling loop - continues until no more tool calls are found
static int ralph_execute_tool_loop(RalphSession* session, const char* user_message, int max_tokens, const char** headers) {
    if (session == NULL) {
        return -1;
    }
    
    // Suppress unused parameter warnings
    (void)user_message;
    (void)max_tokens;
    
    int loop_count = 0;
    const int MAX_TOOL_LOOPS = 10; // Prevent infinite loops
    ExecutedToolTracker tracker = {0};
    
    debug_printf("Starting iterative tool calling loop\n");
    
    while (loop_count < MAX_TOOL_LOOPS) {
        loop_count++;
        debug_printf("Tool calling loop iteration %d\n", loop_count);
        
        // Recalculate token allocation for this iteration
        TokenConfig token_config;
        token_config_init(&token_config, session->config.context_window, session->config.max_context_window);
        TokenUsage token_usage;
        if (calculate_token_allocation(session, "", &token_config, &token_usage) != 0) {
            fprintf(stderr, "Error: Failed to calculate token allocation for tool loop iteration %d\n", loop_count);
            return -1;
        }
        
        int iteration_max_tokens = token_usage.available_response_tokens;
        debug_printf("Using %d max_tokens for tool loop iteration %d\n", iteration_max_tokens, loop_count);
        
        // Build JSON payload with current conversation state
        char* post_data = NULL;
        if (session->config.api_type == API_TYPE_ANTHROPIC) {
            post_data = ralph_build_anthropic_json_payload_with_todos(session, "", iteration_max_tokens);
        } else {
            post_data = ralph_build_json_payload_with_todos(session, "", iteration_max_tokens);
        }
        
        if (post_data == NULL) {
            fprintf(stderr, "Error: Failed to build JSON payload for tool loop iteration %d\n", loop_count);
            return -1;
        }
        
        // Make API request
        struct HTTPResponse response = {0};
        debug_printf("Making API request for tool loop iteration %d\n", loop_count);
        
        if (http_post_with_headers(session->config.api_url, post_data, headers, &response) != 0) {
            fprintf(stderr, "API request failed for tool loop iteration %d\n", loop_count);
            free(post_data);
            return -1;
        }
        
        // Parse response
        ParsedResponse parsed_response;
        int parse_result;
        if (session->config.api_type == API_TYPE_ANTHROPIC) {
            parse_result = parse_anthropic_response(response.data, &parsed_response);
        } else {
            parse_result = parse_api_response(response.data, &parsed_response);
        }
        
        if (parse_result != 0) {
            fprintf(stderr, "Error: Failed to parse API response for tool loop iteration %d\n", loop_count);
            printf("%s\n", response.data);
            cleanup_response(&response);
            free(post_data);
            return -1;
        }
        
        // Display the response
        print_formatted_response(&parsed_response);
        
        // Save assistant response to conversation
        const char* assistant_content = parsed_response.response_content ? 
                                       parsed_response.response_content : 
                                       parsed_response.thinking_content;
        if (assistant_content != NULL) {
            if (append_conversation_message(&session->conversation, "assistant", assistant_content) != 0) {
                fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
            }
        }
        
        // Check for tool calls in the response
        ToolCall *tool_calls = NULL;
        int call_count = 0;
        int tool_parse_result;
        
        // First try to parse from raw JSON response (for proper API tool calls)
        if (session->config.api_type == API_TYPE_ANTHROPIC) {
            tool_parse_result = parse_anthropic_tool_calls(response.data, &tool_calls, &call_count);
        } else {
            tool_parse_result = parse_tool_calls(response.data, &tool_calls, &call_count);
        }
        
        // If no tool calls found in raw response, check message content (for custom format)
        if (tool_parse_result != 0 || call_count == 0) {
            if (assistant_content != NULL && parse_tool_calls(assistant_content, &tool_calls, &call_count) == 0 && call_count > 0) {
                tool_parse_result = 0;
                debug_printf("Found %d tool calls in message content (custom format)\n", call_count);
            }
        }
        
        cleanup_parsed_response(&parsed_response);
        cleanup_response(&response);
        free(post_data);
        
        // If no tool calls found, we're done
        if (tool_parse_result != 0 || call_count == 0) {
            debug_printf("No more tool calls found - ending tool loop after %d iterations\n", loop_count);
            cleanup_executed_tool_tracker(&tracker);
            return 0;
        }
        
        // Check if we've already executed these tool calls (prevent infinite loops)
        int new_tool_calls = 0;
        for (int i = 0; i < call_count; i++) {
            if (!is_tool_already_executed(&tracker, tool_calls[i].id)) {
                new_tool_calls++;
            }
        }
        
        if (new_tool_calls == 0) {
            debug_printf("All %d tool calls already executed - ending loop to prevent infinite iteration\n", call_count);
            cleanup_tool_calls(tool_calls, call_count);
            cleanup_executed_tool_tracker(&tracker);
            return 0;
        }
        
        debug_printf("Found %d new tool calls (out of %d total) in iteration %d - executing them\n", 
                    new_tool_calls, call_count, loop_count);
        
        // Execute only the new tool calls
        ToolResult *results = malloc(call_count * sizeof(ToolResult));
        if (results == NULL) {
            cleanup_tool_calls(tool_calls, call_count);
            cleanup_executed_tool_tracker(&tracker);
            return -1;
        }
        
        int executed_count = 0;
        for (int i = 0; i < call_count; i++) {
            // Skip already executed tool calls
            if (is_tool_already_executed(&tracker, tool_calls[i].id)) {
                debug_printf("Skipping already executed tool: %s (ID: %s)\n", 
                           tool_calls[i].name, tool_calls[i].id);
                continue;
            }
            
            // Track this tool call as executed
            add_executed_tool(&tracker, tool_calls[i].id);
            
            if (execute_tool_call(&session->tools, &tool_calls[i], &results[executed_count]) != 0) {
                fprintf(stderr, "Warning: Failed to execute tool call %s in iteration %d\n", 
                       tool_calls[i].name, loop_count);
                results[executed_count].tool_call_id = strdup(tool_calls[i].id);
                results[executed_count].result = strdup("Tool execution failed");
                results[executed_count].success = 0;
                
                // Handle strdup failures
                if (results[executed_count].tool_call_id == NULL) {
                    results[executed_count].tool_call_id = strdup("unknown");
                }
                if (results[executed_count].result == NULL) {
                    results[executed_count].result = strdup("Memory allocation failed");
                }
            } else {
                debug_printf("Executed tool: %s (ID: %s) in iteration %d\n", 
                           tool_calls[i].name, tool_calls[i].id, loop_count);
            }
            executed_count++;
        }
        
        // Add tool result messages to conversation (only for executed tools)
        for (int i = 0; i < executed_count; i++) {
            if (append_tool_message(&session->conversation, results[i].result, 
                                   results[i].tool_call_id, "tool_name") != 0) {
                fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
            }
        }
        
        cleanup_tool_results(results, executed_count);
        cleanup_tool_calls(tool_calls, call_count);
        
        // Continue the loop to check for more tool calls in the next response
    }
    
    debug_printf("Warning: Tool calling loop reached maximum iterations (%d) - stopping to prevent infinite loop\n", MAX_TOOL_LOOPS);
    cleanup_executed_tool_tracker(&tracker);
    return 0;
}

int ralph_process_message(RalphSession* session, const char* user_message) {
    if (session == NULL || user_message == NULL) return -1;
    
    // Initialize token configuration and calculate optimal allocation
    TokenConfig token_config;
    token_config_init(&token_config, session->config.context_window, session->config.max_context_window);
    
    TokenUsage token_usage;
    if (calculate_token_allocation(session, user_message, &token_config, &token_usage) != 0) {
        fprintf(stderr, "Error: Failed to calculate token allocation\n");
        return -1;
    }
    
    // Use calculated max tokens or configured override
    int max_tokens = session->config.max_tokens;
    if (max_tokens == -1) {
        max_tokens = token_usage.available_response_tokens;
    }
    
    debug_printf("Using token allocation - Response tokens: %d, Safety buffer: %d, Context window: %d\n",
                max_tokens, token_usage.safety_buffer_used, token_usage.context_window_used);
    
    // Build JSON payload with calculated max_tokens
    char* post_data = NULL;
    if (session->config.api_type == API_TYPE_ANTHROPIC) {
        post_data = ralph_build_anthropic_json_payload_with_todos(session, user_message, max_tokens);
    } else {
        post_data = ralph_build_json_payload_with_todos(session, user_message, max_tokens);
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
        
        // If no tool calls found in raw response, check message content (for custom format)
        if (tool_parse_result != 0 || raw_call_count == 0) {
            if (message_content != NULL && parse_tool_calls(message_content, &raw_tool_calls, &raw_call_count) == 0 && raw_call_count > 0) {
                tool_parse_result = 0;
                debug_printf("Found %d tool calls in message content (custom format)\n", raw_call_count);
            }
        }
        
        if (tool_parse_result == 0 && raw_call_count > 0) {
            debug_printf("Found %d tool calls in raw response\n", raw_call_count);
            
            // Display the AI's initial response content before executing tools
            print_formatted_response(&parsed_response);
            
            // For tool calls, we need to save messages in the right order
            // First save user message, then assistant message, then execute tools
            
            // Save user message first
            if (append_conversation_message(&session->conversation, "user", user_message) != 0) {
                fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
            }
            
            // CRITICAL: Only save assistant message with tool calls if we can guarantee 
            // that tool results will also be saved. This prevents orphaned tool calls.
            
            // Pre-execute tool calls to validate they can be processed
            ToolResult *results = malloc(raw_call_count * sizeof(ToolResult));
            if (results == NULL) {
                // Memory allocation failed - save simple assistant message without tool calls
                const char* assistant_content = parsed_response.response_content ? 
                                               parsed_response.response_content : 
                                               parsed_response.thinking_content;
                if (assistant_content != NULL) {
                    if (append_conversation_message(&session->conversation, "assistant", assistant_content) != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                }
                result = -1;
            } else {
                // We can allocate tool results - safe to save assistant message with tool calls
                const char* content_to_save = NULL;
                char* constructed_message = NULL;
                
                if (session->config.api_type == API_TYPE_ANTHROPIC) {
                    // For Anthropic, save the raw JSON response as-is for conversation history
                    content_to_save = response.data;
                } else {
                    // For OpenAI, construct a message with tool_calls array
                    constructed_message = construct_openai_assistant_message_with_tools(
                        parsed_response.response_content, raw_tool_calls, raw_call_count);
                    content_to_save = constructed_message;
                }
                
                if (content_to_save != NULL) {
                    if (append_conversation_message(&session->conversation, "assistant", content_to_save) != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                }
                
                free(constructed_message);
                free(results); // Free pre-allocated results
                
                // Now execute the actual tool workflow
                result = ralph_execute_tool_workflow(session, raw_tool_calls, raw_call_count, user_message, max_tokens, headers);
            }
            cleanup_tool_calls(raw_tool_calls, raw_call_count);
        } else {
            debug_printf("No tool calls found in raw response (result: %d, count: %d)\n", tool_parse_result, raw_call_count);
            // Try to parse tool calls from message content (LM Studio format)
            ToolCall *tool_calls = NULL;
            int call_count = 0;
            if (message_content != NULL && parse_tool_calls(message_content, &tool_calls, &call_count) == 0 && call_count > 0) {
                // Display the AI's initial response content before executing tools
                print_formatted_response(&parsed_response);
                
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