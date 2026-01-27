#include "ralph.h"
#include "context_enhancement.h"
#include "tool_executor.h"
#include "streaming_handler.h"
#include "recap.h"
#include "utils/config.h"
#include "output_formatter.h"
#include "json_output.h"
#include "prompt_loader.h"
#include "todo_tool.h"
#include "todo_display.h"
#include "debug_output.h"
#include "api_common.h"
#include "../llm/embeddings_service.h"
#include "token_manager.h"
#include "conversation_compactor.h"
#include "session_manager.h"
#include "model_capabilities.h"
#include "python_tool.h"
#include "llm_provider.h"
#include "uuid_utils.h"
#include "../db/task_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include "json_escape.h"

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

char* ralph_build_json_payload_with_todos(const RalphSession* session,
                                         const char* user_message, int max_tokens) {
    if (session == NULL) return NULL;

    char* final_prompt = build_enhanced_prompt_with_context(session, user_message);
    if (final_prompt == NULL) return NULL;

    char* result = ralph_build_json_payload(session->session_data.config.model, final_prompt,
                                          &session->session_data.conversation, user_message,
                                          session->session_data.config.max_tokens_param, max_tokens,
                                          &session->tools);

    free(final_prompt);
    return result;
}

char* ralph_build_anthropic_json_payload_with_todos(const RalphSession* session,
                                                   const char* user_message, int max_tokens) {
    if (session == NULL) return NULL;

    char* final_prompt = build_enhanced_prompt_with_context(session, user_message);
    if (final_prompt == NULL) return NULL;

    char* result = ralph_build_anthropic_json_payload(session->session_data.config.model, final_prompt,
                                                    &session->session_data.conversation, user_message,
                                                    max_tokens, &session->tools);

    free(final_prompt);
    return result;
}

int ralph_init_session(RalphSession* session) {
    if (session == NULL) return -1;

    // Generate unique session ID
    if (uuid_generate_v4(session->session_id) != 0) {
        fprintf(stderr, "Warning: Failed to generate session ID, using fallback\n");
        snprintf(session->session_id, sizeof(session->session_id), "fallback-%ld", (long)time(NULL));
    }

    // Initialize task store singleton (for persistent task storage)
    if (task_store_get_instance() == NULL) {
        fprintf(stderr, "Warning: Task store unavailable, using in-memory tasks only\n");
    }

    // Initialize session data
    session_data_init(&session->session_data);
    
    // Load conversation history into session data
    if (load_conversation_history(&session->session_data.conversation) != 0) {
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
        cleanup_conversation_history(&session->session_data.conversation);
        cleanup_tool_registry(&session->tools);
        return -1;
    }
    
    // Register todo tools (TodoRead and TodoWrite) with the tool registry
    if (register_todo_tool(&session->tools, &session->todo_list) != 0) {
        fprintf(stderr, "Warning: Failed to register todo tools\n");
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
    
    // Initialize MCP client
    if (mcp_client_init(&session->mcp_client) != 0) {
        fprintf(stderr, "Warning: Failed to initialize MCP client\n");
    } else {
        // Try to find and load MCP configuration
        char config_path[512];
        if (mcp_find_config_path(config_path, sizeof(config_path)) == 0) {
            if (mcp_client_load_config(&session->mcp_client, config_path) == 0) {
                // Connect to MCP servers
                if (mcp_client_connect_servers(&session->mcp_client) == 0) {
                    // Register MCP tools with the tool registry
                    if (mcp_client_register_tools(&session->mcp_client, &session->tools) != 0) {
                        fprintf(stderr, "Warning: Failed to register MCP tools\n");
                    }
                } else {
                    // Silent fail - MCP connection is optional
                }
            } else {
                // Silent fail - MCP is optional
            }
        }
        // No warning if config file not found - MCP is optional
    }

    // Initialize subagent manager
    if (subagent_manager_init(&session->subagent_manager) != 0) {
        fprintf(stderr, "Warning: Failed to initialize subagent manager\n");
    } else {
        // Register subagent tools (they internally check is_subagent_process to prevent nesting)
        if (register_subagent_tool(&session->tools, &session->subagent_manager) != 0) {
            fprintf(stderr, "Warning: Failed to register subagent tool\n");
        }
        if (register_subagent_status_tool(&session->tools, &session->subagent_manager) != 0) {
            fprintf(stderr, "Warning: Failed to register subagent_status tool\n");
        }
    }

    // Initialize approval gates (non-critical - gate enforcement is optional)
    if (approval_gate_init(&session->gate_config) != 0) {
        fprintf(stderr, "Warning: Failed to initialize approval gates\n");
    } else {
        // Detect if we're running interactively (has TTY)
        approval_gate_detect_interactive(&session->gate_config);
    }

    return 0;
}

void ralph_cleanup_session(RalphSession* session) {
    if (session == NULL) return;

    // Cleanup streaming handler resources (provider registry)
    streaming_handler_cleanup();

    // Shutdown Python interpreter if it was initialized
    python_interpreter_shutdown();

    // Cleanup approval gates
    approval_gate_cleanup(&session->gate_config);

    // Cleanup subagent manager (kills any running subagents)
    subagent_manager_cleanup(&session->subagent_manager);

    // Cleanup MCP client
    mcp_client_cleanup(&session->mcp_client);
    
    // Clear global tool reference before destroying the todo list
    clear_todo_tool_reference();
    
    // Cleanup todo display system
    todo_display_cleanup();
    
    // Cleanup todo list first (before tool registry which might reference it)
    todo_list_destroy(&session->todo_list);
    
    // Cleanup tools registry
    cleanup_tool_registry(&session->tools);
    
    // Cleanup session data (handles conversation and config cleanup)
    session_data_cleanup(&session->session_data);
    
    // Cleanup configuration system
    config_cleanup();
}

int ralph_load_config(RalphSession* session) {
    if (session == NULL) return -1;
    
    // Initialize the new configuration system
    if (config_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize configuration system\n");
        return -1;
    }
    
    ralph_config_t *config = config_get();
    if (!config) {
        fprintf(stderr, "Error: Failed to get configuration instance\n");
        return -1;
    }
    
    // Reinitialize embeddings service with new configuration
    embeddings_service_reinitialize();
    
    // Try to load system prompt from PROMPT.md (optional)
    load_system_prompt(&session->session_data.config.system_prompt);
    
    // Copy configuration values from global config to session config
    if (config->api_url) {
        session->session_data.config.api_url = strdup(config->api_url);
        if (session->session_data.config.api_url == NULL) return -1;
    }
    
    if (config->model) {
        session->session_data.config.model = strdup(config->model);
        if (session->session_data.config.model == NULL) return -1;
    }
    
    if (config->api_key) {
        session->session_data.config.api_key = strdup(config->api_key);
        if (session->session_data.config.api_key == NULL) return -1;
    }
    
    session->session_data.config.context_window = config->context_window;
    session->session_data.config.max_tokens = config->max_tokens;
    session->session_data.config.enable_streaming = config->enable_streaming;
    
    // Determine API type and correct parameter name for max tokens
    if (strstr(session->session_data.config.api_url, "api.openai.com") != NULL) {
        session->session_data.config.api_type = API_TYPE_OPENAI;
        session->session_data.config.max_tokens_param = "max_completion_tokens";
    } else if (strstr(session->session_data.config.api_url, "api.anthropic.com") != NULL) {
        session->session_data.config.api_type = API_TYPE_ANTHROPIC;
        session->session_data.config.max_tokens_param = "max_tokens";
    } else {
        session->session_data.config.api_type = API_TYPE_LOCAL;
        session->session_data.config.max_tokens_param = "max_tokens";
    }
    
    // Auto-configure context window from model capabilities if using defaults
    if (session->session_data.config.context_window == 8192) { // Default value
        ModelRegistry* registry = get_model_registry();
        if (registry && session->session_data.config.model) {
            ModelCapabilities* model = detect_model_capabilities(registry, session->session_data.config.model);
            if (model && model->max_context_length > 0) {
                session->session_data.config.context_window = model->max_context_length;
                debug_printf("Auto-configured context window from model capabilities: %d tokens for model %s\n",
                            model->max_context_length, session->session_data.config.model);
                debug_printf("🔧 Auto-configured context window: %d tokens (based on %s capabilities)\n",
                       model->max_context_length, session->session_data.config.model);
            } else {
                debug_printf("Using default context window (%d tokens) - no model capabilities found for model %s\n",
                            session->session_data.config.context_window, 
                            session->session_data.config.model ? session->session_data.config.model : "unknown");
            }
        }
    }
    
    return 0;
}

// =============================================================================
// Tool Execution (delegated to tool_executor module)
// =============================================================================

// Backward-compatible wrapper - delegates to tool_executor module
int ralph_execute_tool_workflow(RalphSession* session, ToolCall* tool_calls, int call_count,
                               const char* user_message, int max_tokens, const char** headers) {
    return tool_executor_run_workflow(session, tool_calls, call_count, user_message, max_tokens, headers);
}

int ralph_process_message(RalphSession* session, const char* user_message) {
    if (session == NULL || user_message == NULL) return -1;
    
    // Initialize token configuration and calculate optimal allocation
    TokenConfig token_config;
    token_config_init(&token_config, session->session_data.config.context_window);
    
    TokenUsage token_usage;
    if (manage_conversation_tokens(session, user_message, &token_config, &token_usage) != 0) {
        fprintf(stderr, "Error: Failed to calculate token allocation\n");
        return -1;
    }
    
    // Use calculated max tokens or configured override
    int max_tokens = session->session_data.config.max_tokens;
    if (max_tokens == -1) {
        max_tokens = token_usage.available_response_tokens;
    }
    
    debug_printf("Using token allocation - Response tokens: %d, Safety buffer: %d, Context window: %d\n",
                max_tokens, token_usage.safety_buffer_used, token_usage.context_window_used);
    
    // Build JSON payload with calculated max_tokens
    char* post_data = NULL;
    if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
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
    
    if (session->session_data.config.api_key != NULL) {
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            // Anthropic uses x-api-key header
            int ret = snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", session->session_data.config.api_key);
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
            int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", session->session_data.config.api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                fprintf(stderr, "Error: Authorization header too long\n");
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
        }
    }

    // Check if streaming is supported for this provider and use it if available
    ProviderRegistry* provider_registry = streaming_get_provider_registry();
    LLMProvider* provider = NULL;
    if (provider_registry != NULL) {
        provider = detect_provider_for_url(provider_registry, session->session_data.config.api_url);
    }

    // Use streaming if enabled in config and provider supports it
    bool streaming_enabled = session->session_data.config.enable_streaming;
    bool provider_supports_streaming = provider != NULL &&
                                       provider->supports_streaming != NULL &&
                                       provider->supports_streaming(provider) &&
                                       provider->build_streaming_request_json != NULL &&
                                       provider->parse_stream_event != NULL;

    if (streaming_enabled && provider_supports_streaming) {
        debug_printf("Using streaming mode for provider: %s\n", provider->capabilities.name);
        free(post_data);  // Free the non-streaming payload, streaming function builds its own

        curl_global_init(CURL_GLOBAL_DEFAULT);
        int result = streaming_process_message(session, user_message, max_tokens, headers);
        curl_global_cleanup();
        return result;
    }

    if (!streaming_enabled) {
        debug_printf("Using buffered mode (streaming disabled via configuration)\n");
    } else {
        debug_printf("Using buffered mode (provider does not support streaming)\n");
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    debug_printf("Making API request to %s\n", session->session_data.config.api_url);
    debug_printf("POST data: %s\n\n", post_data);

    // Display subtle thinking indicator to user (skip in JSON mode)
    if (!session->session_data.config.json_output_mode) {
        fprintf(stdout, "\033[36m•\033[0m ");
        fflush(stdout);
    }

    struct HTTPResponse response = {0};
    int result = -1;

    if (http_post_with_headers(session->session_data.config.api_url, post_data, headers, &response) == 0) {
        // Check for NULL response data
        if (response.data == NULL) {
            fprintf(stderr, "Error: Empty response from API\n");
            cleanup_response(&response);
            free(post_data);
            curl_global_cleanup();
            return -1;
        }

        debug_printf_json("Got API response: ", response.data);
        // Parse the response based on API type
        ParsedResponse parsed_response;
        int parse_result;
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            parse_result = parse_anthropic_response(response.data, &parsed_response);
        } else {
            parse_result = parse_api_response(response.data, &parsed_response);
        }
        
        if (parse_result != 0) {
            // Clear the thinking indicator before showing error
            fprintf(stdout, "\r\033[K");
            fflush(stdout);
            
            // Check for common API key errors and provide user-friendly messages
            if (strstr(response.data, "didn't provide an API key") != NULL || 
                strstr(response.data, "Incorrect API key") != NULL ||
                strstr(response.data, "invalid_api_key") != NULL) {
                fprintf(stderr, "❌ API key missing or invalid.\n");
                fprintf(stderr, "   Please add your API key to ralph.config.json\n");
            } else if (strstr(response.data, "\"error\"") != NULL) {
                fprintf(stderr, "❌ API request failed. Check your configuration.\n");
                if (debug_enabled) {
                    fprintf(stderr, "Debug: %s\n", response.data);
                }
            } else {
                fprintf(stderr, "Error: Failed to parse API response\n");
                printf("%s\n", response.data);  // Fallback to raw output
            }
            cleanup_response(&response);
            free(post_data);
            curl_global_cleanup();
            return -1;
        }
        
        // Clear the thinking indicator on success (skip in JSON mode)
        if (!session->session_data.config.json_output_mode) {
            fprintf(stdout, "\r\033[K");
            fflush(stdout);
        }
        
        const char* message_content = parsed_response.response_content ? 
                                     parsed_response.response_content : 
                                     parsed_response.thinking_content;
        
        // Try to parse tool calls from raw JSON response
        ToolCall *raw_tool_calls = NULL;
        int raw_call_count = 0;
        int tool_parse_result;
        
        // Use model capabilities to parse tool calls
        ModelRegistry* model_registry = get_model_registry();
        tool_parse_result = parse_model_tool_calls(model_registry, session->session_data.config.model,
                                                  response.data, &raw_tool_calls, &raw_call_count);
        
        // If no tool calls found in raw response, check message content (for custom format)
        if (tool_parse_result != 0 || raw_call_count == 0) {
            // Try parsing from content as fallback (for custom format)
            if (message_content != NULL && parse_model_tool_calls(model_registry, session->session_data.config.model,
                                                                  message_content, &raw_tool_calls, &raw_call_count) == 0 && raw_call_count > 0) {
                tool_parse_result = 0;
                debug_printf("Found %d tool calls in message content (custom format)\n", raw_call_count);
            }
        }
        
        if (tool_parse_result == 0 && raw_call_count > 0) {
            debug_printf("Found %d tool calls in raw response\n", raw_call_count);
            debug_printf("Response content before display: [%s]\n", 
                        parsed_response.response_content ? parsed_response.response_content : "NULL");
            
            // Display the AI's initial response content before executing tools
            print_formatted_response_improved(&parsed_response);
            
            // For tool calls, we need to save messages in the right order
            // First save user message, then assistant message, then execute tools
            
            // Save user message first
            if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
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
                    if (append_conversation_message(&session->session_data.conversation, "assistant", assistant_content) != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                }
                result = -1;
            } else {
                // We can allocate tool results - safe to save assistant message with tool calls
                const char* content_to_save = NULL;
                char* constructed_message = NULL;
                
                if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
                    // For Anthropic, save the raw JSON response as-is for conversation history
                    content_to_save = response.data;
                } else {
                    // For OpenAI, construct a message with tool_calls array
                    constructed_message = construct_openai_assistant_message_with_tools(
                        parsed_response.response_content, raw_tool_calls, raw_call_count);
                    content_to_save = constructed_message;
                }

                if (content_to_save != NULL) {
                    if (append_conversation_message(&session->session_data.conversation, "assistant", content_to_save) != 0) {
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
                print_formatted_response_improved(&parsed_response);
                
                // Save user message first for LM Studio format too
                if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
                }
                result = ralph_execute_tool_workflow(session, tool_calls, call_count, user_message, max_tokens, headers);
                cleanup_tool_calls(tool_calls, call_count);
            } else {
                // No tool calls - normal response handling
                debug_printf("No tool calls path - response_content: [%s]\n", 
                            parsed_response.response_content ? parsed_response.response_content : "NULL");
                print_formatted_response_improved(&parsed_response);
                
                // Save user message
                if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
                }
                
                // Use response_content for the assistant's message (this is the main content without thinking)
                const char* assistant_content = parsed_response.response_content ?
                                               parsed_response.response_content :
                                               parsed_response.thinking_content;
                if (assistant_content != NULL) {
                    if (append_conversation_message(&session->session_data.conversation, "assistant", assistant_content) != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }

                    // Output text response in JSON mode
                    if (session->session_data.config.json_output_mode) {
                        json_output_assistant_text(assistant_content,
                                                   parsed_response.prompt_tokens,
                                                   parsed_response.completion_tokens);
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

// Manage conversation size using compaction when needed
int manage_conversation_tokens(RalphSession* session, const char* user_message,
                              TokenConfig* config, TokenUsage* usage) {
    if (session == NULL || config == NULL || usage == NULL) {
        return -1;
    }
    
    // Update tool count in session data
    session->session_data.tool_count = session->tools.function_count;
    
    // First, try normal token allocation using the embedded SessionData
    int result = calculate_token_allocation(&session->session_data, user_message, config, usage);
    if (result != 0) {
        return result;
    }
    
    // Check if background compaction should be triggered to maintain performance
    CompactionConfig compact_config;
    compaction_config_init(&compact_config);
    
    // Set background threshold based on context window size
    compact_config.background_threshold = (int)(config->context_window * 0.4f);
    
    CompactionResult background_result = {0};
    int background_status = background_compact_conversation(&session->session_data, &compact_config, &background_result);
    
    if (background_status == 0 && background_result.tokens_saved > 0) {
        // Background trimming succeeded, recalculate token allocation
        debug_printf("Background trimming saved %d tokens, recalculating allocation\n", background_result.tokens_saved);
        
        cleanup_compaction_result(&background_result);
        
        // Recalculate with the trimmed conversation
        result = calculate_token_allocation(&session->session_data, user_message, config, usage);
        if (result != 0) {
            return result;
        }
        debug_printf("After background trimming: %d response tokens available\n", usage->available_response_tokens);
    } else if (background_status == 0) {
        // No background trimming was needed (tokens_saved == 0)
        cleanup_compaction_result(&background_result);
    }
    
    // If we have insufficient tokens for a good response, try emergency trimming
    if (usage->available_response_tokens < config->min_response_tokens * 2) {
        debug_printf("Available response tokens (%d) below comfortable threshold, attempting emergency trimming\n",
                    usage->available_response_tokens);
        
        // Calculate target token count (aim for 70% of context window)
        int target_tokens = (int)(config->context_window * 0.7);
        
        // Attempt emergency trimming using the embedded SessionData
        CompactionResult compact_result = {0};
        int compact_status = compact_conversation(&session->session_data, &compact_config, target_tokens, &compact_result);
        
        if (compact_status == 0 && compact_result.tokens_saved > 0) {
            // Trimming succeeded, recalculate token allocation
            debug_printf("Trimming saved %d tokens, recalculating allocation\n", compact_result.tokens_saved);
            
            cleanup_compaction_result(&compact_result);
            
            // Recalculate with the trimmed conversation (conversation is already updated in session_data)
            result = calculate_token_allocation(&session->session_data, user_message, config, usage);
            if (result == 0) {
                debug_printf("After trimming: %d response tokens available\n", usage->available_response_tokens);
            }
            
            return result;
        } else {
            // Trimming failed or wasn't helpful
            debug_printf("Trimming failed or ineffective, using original allocation\n");
            cleanup_compaction_result(&compact_result);
        }
    }
    
    return 0;
}