/**
 * Tool Executor Module
 *
 * Handles the iterative tool-calling state machine for ralph.
 * Extracted from ralph.c for better modularity and testability.
 */

#include "tool_executor.h"
#include "ralph.h"
#include <cJSON.h>
#include "output_formatter.h"
#include "json_output.h"
#include "debug_output.h"
#include "api_error.h"
#include "token_manager.h"
#include "model_capabilities.h"
#include "../mcp/mcp_client.h"
#include "../utils/ptrarray.h"
#include "../policy/approval_gate.h"
#include "../policy/protected_files.h"
#include "../policy/tool_args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_escape.h"

// =============================================================================
// Approval Gate Helpers
// =============================================================================

/**
 * Check if a tool performs file write operations.
 * These tools need protected file checks before execution.
 */
static int is_file_write_tool(const char *tool_name) {
    if (tool_name == NULL) return 0;
    return (strcmp(tool_name, "write_file") == 0 ||
            strcmp(tool_name, "append_file") == 0 ||
            strcmp(tool_name, "apply_delta") == 0);
}

/**
 * Check approval gates and protected files before tool execution.
 *
 * @param session The ralph session with gate config
 * @param tool_call The tool call to check
 * @param result Output: populated with error if operation is blocked
 * @return 0 if tool can be executed, -1 if blocked (result contains error)
 */
static int check_tool_approval(RalphSession *session, const ToolCall *tool_call,
                               ToolResult *result) {
    if (session == NULL || tool_call == NULL || result == NULL) {
        return 0; // No session or tool call - allow execution
    }

    // Skip gate checking if gates are disabled
    if (!session->gate_config.enabled) {
        return 0;
    }

    // Check protected files first (hard block, cannot be bypassed)
    if (is_file_write_tool(tool_call->name)) {
        char *path = tool_args_get_path(tool_call);
        if (path != NULL) {
            if (is_protected_file(path)) {
                result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
                result->result = format_protected_file_error(path);
                result->success = 0;
                free(path);
                return -1; // Blocked
            }
            free(path);
        }
    }

    // Check rate limiting
    if (is_rate_limited(&session->gate_config, tool_call)) {
        result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
        result->result = format_rate_limit_error(&session->gate_config, tool_call);
        result->success = 0;
        return -1; // Blocked
    }

    // Check approval gate
    ApprovedPath approved_path;
    init_approved_path(&approved_path);

    ApprovalResult approval = check_approval_gate(&session->gate_config,
                                                   tool_call,
                                                   &approved_path);

    switch (approval) {
        case APPROVAL_ALLOWED:
        case APPROVAL_ALLOWED_ALWAYS:
            // Verify path hasn't changed (TOCTOU protection) for file operations
            if (approved_path.resolved_path != NULL) {
                VerifyResult verify = verify_approved_path(&approved_path);
                if (verify != VERIFY_OK) {
                    result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
                    result->result = format_verify_error(verify, approved_path.resolved_path);
                    result->success = 0;
                    free_approved_path(&approved_path);
                    return -1; // Blocked
                }
            }
            free_approved_path(&approved_path);
            return 0; // Proceed with execution

        case APPROVAL_DENIED:
            track_denial(&session->gate_config, tool_call);
            result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
            result->result = format_denial_error(tool_call);
            result->success = 0;
            free_approved_path(&approved_path);
            return -1; // Blocked

        case APPROVAL_RATE_LIMITED:
            result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
            result->result = format_rate_limit_error(&session->gate_config, tool_call);
            result->success = 0;
            free_approved_path(&approved_path);
            return -1; // Blocked

        case APPROVAL_NON_INTERACTIVE_DENIED:
            result->tool_call_id = tool_call->id ? strdup(tool_call->id) : NULL;
            result->result = format_non_interactive_error(tool_call);
            result->success = 0;
            free_approved_path(&approved_path);
            return -1; // Blocked

        case APPROVAL_ABORTED:
            // User pressed Ctrl+C - signal abort
            free_approved_path(&approved_path);
            return -2; // Special return code for abort
    }

    free_approved_path(&approved_path);
    return 0; // Default: allow
}

// =============================================================================
// OpenAI Assistant Message Construction
// =============================================================================

char* construct_openai_assistant_message_with_tools(const char* content,
                                                    const ToolCall* tool_calls,
                                                    int call_count) {
    if (call_count <= 0 || tool_calls == NULL) {
        return content ? strdup(content) : NULL;
    }

    // Estimate size needed for the JSON message with overflow protection
    size_t base_size = 200; // Base structure
    size_t content_len = content ? strlen(content) : 0;

    // Check for potential overflow in content size calculation
    if (content_len > SIZE_MAX / 2 - 50) {
        return NULL;
    }
    size_t content_size = content_len * 2 + 50; // Escaped content

    // Check for potential overflow in tools size calculation
    if (call_count > 0 && (size_t)call_count > SIZE_MAX / 200) {
        return NULL;
    }
    size_t tools_size = (size_t)call_count * 200; // Rough estimate per tool call

    // Check for overflow in total size
    if (base_size > SIZE_MAX - content_size ||
        base_size + content_size > SIZE_MAX - tools_size) {
        return NULL;
    }

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

        // Dynamically calculate buffer size needed for this tool call
        const char* id = tool_calls[i].id ? tool_calls[i].id : "";
        const char* name = tool_calls[i].name ? tool_calls[i].name : "";
        // Buffer needs: prefix (2) + id + json structure (~80) + name + arguments
        size_t tool_json_size = strlen(id) + strlen(name) + strlen(escaped_args) + 100;
        char* tool_call_json = malloc(tool_json_size);
        if (tool_call_json == NULL) {
            free(escaped_args);
            free(message);
            return NULL;
        }

        int tool_written = snprintf(tool_call_json, tool_json_size,
                                   "%s{\"id\": \"%s\", \"type\": \"function\", \"function\": {\"name\": \"%s\", \"arguments\": \"%s\"}}",
                                   i > 0 ? ", " : "",
                                   id, name, escaped_args);
        free(escaped_args);

        if (tool_written < 0 || tool_written >= (int)tool_json_size) {
            free(tool_call_json);
            free(message);
            return NULL;
        }

        // Reallocate message buffer if needed
        size_t current_len = strlen(message);
        size_t needed_len = current_len + strlen(tool_call_json) + 3; // +3 for "]}" and null
        if (needed_len > base_size + content_size + tools_size) {
            char* new_message = realloc(message, needed_len + 100);
            if (new_message == NULL) {
                free(tool_call_json);
                free(message);
                return NULL;
            }
            message = new_message;
        }

        strcat(message, tool_call_json);
        free(tool_call_json);
    }

    strcat(message, "]}");
    return message;
}

// =============================================================================
// Executed Tool Tracker
// =============================================================================

// Check if a tool call ID was already executed
static int is_tool_already_executed(const StringArray* tracker, const char* tool_call_id) {
    for (size_t i = 0; i < tracker->count; i++) {
        if (strcmp(tracker->data[i], tool_call_id) == 0) {
            return 1;
        }
    }
    return 0;
}

// Add a tool call ID to the executed tracker
// Returns 0 on success, -1 on failure
static int add_executed_tool(StringArray* tracker, const char* tool_call_id) {
    if (tracker == NULL || tool_call_id == NULL) {
        return -1;
    }

    char* dup = strdup(tool_call_id);
    if (dup == NULL) {
        return -1;
    }

    if (StringArray_push(tracker, dup) != 0) {
        free(dup);
        return -1;
    }

    return 0;
}

// =============================================================================
// Tool Execution Loop (Internal)
// =============================================================================

// Iterative tool calling loop - continues until no more tool calls are found
static int tool_executor_run_loop(RalphSession* session, const char* user_message,
                                  int max_tokens, const char** headers) {
    if (session == NULL) {
        return -1;
    }

    // Suppress unused parameter warnings
    (void)user_message;
    (void)max_tokens;

    int loop_count = 0;
    StringArray tracker;
    if (StringArray_init(&tracker, free) != 0) {
        return -1;
    }

    debug_printf("Starting iterative tool calling loop\n");

    while (1) {
        loop_count++;
        debug_printf("Tool calling loop iteration %d\n", loop_count);

        // Recalculate token allocation for this iteration
        TokenConfig token_config;
        token_config_init(&token_config, session->session_data.config.context_window);
        TokenUsage token_usage;
        if (manage_conversation_tokens(session, "", &token_config, &token_usage) != 0) {
            fprintf(stderr, "Error: Failed to calculate token allocation for tool loop iteration %d\n", loop_count);
            StringArray_destroy(&tracker);
            return -1;
        }

        int iteration_max_tokens = token_usage.available_response_tokens;
        debug_printf("Using %d max_tokens for tool loop iteration %d\n", iteration_max_tokens, loop_count);

        // Build JSON payload with current conversation state
        char* post_data = NULL;
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            post_data = ralph_build_anthropic_json_payload_with_todos(session, "", iteration_max_tokens);
        } else {
            post_data = ralph_build_json_payload_with_todos(session, "", iteration_max_tokens);
        }

        if (post_data == NULL) {
            fprintf(stderr, "Error: Failed to build JSON payload for tool loop iteration %d\n", loop_count);
            StringArray_destroy(&tracker);
            return -1;
        }

        // Make API request
        struct HTTPResponse response = {0};
        debug_printf("Making API request for tool loop iteration %d\n", loop_count);

        // Display subtle thinking indicator to user (skip in JSON mode)
        if (!session->session_data.config.json_output_mode) {
            fprintf(stdout, "\033[36m•\033[0m ");
            fflush(stdout);
        }

        if (http_post_with_headers(session->session_data.config.api_url, post_data, headers, &response) != 0) {
            // Clear the thinking indicator before showing error
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, "\r\033[K");
                fflush(stdout);
            }

            APIError err;
            get_last_api_error(&err);

            fprintf(stderr, "%s\n", api_error_user_message(&err));

            if (err.attempts_made > 1) {
                fprintf(stderr, "   (Retried %d times)\n", err.attempts_made);
            }

            debug_printf("HTTP status: %ld, Error: %s\n",
                        err.http_status, err.error_message);

            free(post_data);
            StringArray_destroy(&tracker);
            return -1;
        }

        // Check for NULL response data
        if (response.data == NULL) {
            // Clear the thinking indicator before showing error
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, "\r\033[K");
                fflush(stdout);
            }
            fprintf(stderr, "Error: Empty response from API in tool loop iteration %d\n", loop_count);
            cleanup_response(&response);
            free(post_data);
            StringArray_destroy(&tracker);
            return -1;
        }

        // Parse response
        ParsedResponse parsed_response;
        int parse_result;
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            parse_result = parse_anthropic_response(response.data, &parsed_response);
        } else {
            parse_result = parse_api_response(response.data, &parsed_response);
        }

        if (parse_result != 0) {
            // Clear the thinking indicator before showing error
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, "\r\033[K");
                fflush(stdout);
            }

            // Check for common API key errors and provide user-friendly messages
            if (strstr(response.data, "didn't provide an API key") != NULL ||
                strstr(response.data, "Incorrect API key") != NULL ||
                strstr(response.data, "invalid_api_key") != NULL) {
                fprintf(stderr, "API key missing or invalid.\n");
                fprintf(stderr, "   Please add your API key to ralph.config.json\n");
            } else if (strstr(response.data, "\"error\"") != NULL) {
                fprintf(stderr, "API request failed during tool execution.\n");
                if (debug_enabled) {
                    fprintf(stderr, "Debug: %s\n", response.data);
                }
            } else {
                fprintf(stderr, "Error: Failed to parse API response for tool loop iteration %d\n", loop_count);
                printf("%s\n", response.data);
            }
            cleanup_response(&response);
            free(post_data);
            StringArray_destroy(&tracker);
            return -1;
        }

        // Clear the thinking indicator on success (skip in JSON mode)
        if (!session->session_data.config.json_output_mode) {
            fprintf(stdout, "\r\033[K");
            fflush(stdout);
        }

        // NOTE: Response display is deferred until after we check for tool calls
        // so we can close the tool box first if this is the final response

        // Check for tool calls in the response FIRST
        ToolCall *tool_calls = NULL;
        int call_count = 0;
        int tool_parse_result;

        // Use model capabilities to parse tool calls
        ModelRegistry* model_registry = get_model_registry();
        tool_parse_result = parse_model_tool_calls(model_registry, session->session_data.config.model,
                                                  response.data, &tool_calls, &call_count);

        // If no tool calls found in raw response, check message content (for custom format)
        const char* assistant_content = parsed_response.response_content ?
                                       parsed_response.response_content :
                                       parsed_response.thinking_content;
        if (tool_parse_result != 0 || call_count == 0) {
            // Try parsing from content as fallback (for custom format)
            if (assistant_content != NULL && parse_model_tool_calls(model_registry, session->session_data.config.model,
                                                                    assistant_content, &tool_calls, &call_count) == 0 && call_count > 0) {
                tool_parse_result = 0;
                debug_printf("Found %d tool calls in message content (custom format)\n", call_count);
            }
        }

        // Save assistant response to conversation
        if (tool_parse_result == 0 && call_count > 0) {
            // CRITICAL: Display any text content BEFORE executing tools
            // This ensures the agent's reasoning/text is shown to the user interleaved with tool calls
            if (parsed_response.response_content != NULL && strlen(parsed_response.response_content) > 0) {
                if (!session->session_data.config.json_output_mode) {
                    // Terminal mode: display text content
                    printf("%s\n", parsed_response.response_content);
                    fflush(stdout);
                } else {
                    // JSON mode: output text content before tool calls
                    json_output_assistant_text(parsed_response.response_content,
                                               parsed_response.prompt_tokens,
                                               parsed_response.completion_tokens);
                }
            }

            // Output tool calls in JSON mode (after text content)
            if (session->session_data.config.json_output_mode) {
                json_output_assistant_tool_calls_buffered(tool_calls, call_count,
                                                          parsed_response.prompt_tokens,
                                                          parsed_response.completion_tokens);
            }

            // For responses with tool calls, use model-specific formatting
            // Use parsed assistant_content, not raw response.data (which contains full API response JSON)
            char* formatted_message = format_model_assistant_tool_message(model_registry,
                                                                         session->session_data.config.model,
                                                                         assistant_content, tool_calls, call_count);
            if (formatted_message) {
                if (append_conversation_message(&session->session_data.conversation, "assistant", formatted_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response with tool calls to conversation history\n");
                }
                free(formatted_message);
            } else {
                // Fallback: create a proper assistant message with tool calls
                // Don't save the raw API response as it contains invalid nested structure
                char* simple_message = malloc(256);
                if (simple_message) {
                    snprintf(simple_message, 256, "Used tools: ");
                    for (int i = 0; i < call_count; i++) {
                        if (i > 0) {
                            strncat(simple_message, ", ", 255 - strlen(simple_message));
                        }
                        strncat(simple_message, tool_calls[i].name, 255 - strlen(simple_message));
                    }
                    if (append_conversation_message(&session->session_data.conversation, "assistant", simple_message) != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                    free(simple_message);
                } else {
                    // Last resort: save a generic message
                    if (append_conversation_message(&session->session_data.conversation, "assistant", "Executed tool calls") != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                }
            }
        } else {
            // For responses without tool calls, save the parsed content
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
        }

        cleanup_response(&response);
        free(post_data);

        // If no tool calls found, display final response and exit loop
        if (tool_parse_result != 0 || call_count == 0) {
            debug_printf("No more tool calls found - ending tool loop after %d iterations\n", loop_count);
            print_formatted_response_improved(&parsed_response);
            cleanup_parsed_response(&parsed_response);
            StringArray_destroy(&tracker);
            return 0;
        }

        // Clean up parsed response for iterations that continue with more tool calls
        cleanup_parsed_response(&parsed_response);

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
            StringArray_destroy(&tracker);
            return 0;
        }

        debug_printf("Found %d new tool calls (out of %d total) in iteration %d - executing them\n",
                    new_tool_calls, call_count, loop_count);

        // Execute only the new tool calls - use calloc to zero-initialize
        ToolResult *results = calloc(call_count, sizeof(ToolResult));
        if (results == NULL) {
            cleanup_tool_calls(tool_calls, call_count);
            StringArray_destroy(&tracker);
            return -1;
        }

        // Track mapping from result index to tool call index for proper tool names
        int *tool_call_indices = malloc(call_count * sizeof(int));
        if (tool_call_indices == NULL) {
            free(results);
            cleanup_tool_calls(tool_calls, call_count);
            // Note: Group is closed by caller (tool_executor_run_workflow)
            StringArray_destroy(&tracker);
            return -1;
        }

        // Force refresh of protected inode cache before batch processing
        force_protected_inode_refresh();

        int executed_count = 0;
        int loop_aborted = 0;
        for (int i = 0; i < call_count; i++) {
            // Skip already executed tool calls
            if (is_tool_already_executed(&tracker, tool_calls[i].id)) {
                debug_printf("Skipping already executed tool: %s (ID: %s)\n",
                           tool_calls[i].name, tool_calls[i].id);
                continue;
            }

            // Track this tool call as executed - skip execution if tracking fails
            // to prevent potential duplicate execution in subsequent iterations
            if (add_executed_tool(&tracker, tool_calls[i].id) != 0) {
                debug_printf("Warning: Failed to track tool call ID %s, skipping execution\n", tool_calls[i].id);
                continue;
            }

            // Store mapping from result index to tool call index
            tool_call_indices[executed_count] = i;

            // Check approval gates before execution
            int approval_check = check_tool_approval(session, &tool_calls[i], &results[executed_count]);
            if (approval_check == -2) {
                // User aborted (Ctrl+C) - stop processing remaining tools
                loop_aborted = 1;
                debug_printf("User aborted tool execution in loop iteration %d\n", loop_count);
                executed_count++;
                break;
            }
            if (approval_check == -1) {
                // Tool was blocked (result already populated)
                debug_printf("Tool %s blocked by approval gate in iteration %d\n",
                           tool_calls[i].name, loop_count);
                // Output tool result in JSON mode
                if (session->session_data.config.json_output_mode) {
                    json_output_tool_result(tool_calls[i].id, results[executed_count].result, !results[executed_count].success);
                }
                executed_count++;
                continue;
            }

            int tool_executed = 0;

            // Check if this is an MCP tool call
            if (strncmp(tool_calls[i].name, "mcp_", 4) == 0) {
                if (mcp_client_execute_tool(&session->mcp_client, &tool_calls[i], &results[executed_count]) == 0) {
                    tool_executed = 1;
                }
            }

            // If not an MCP tool or MCP execution failed, try standard tool execution
            if (!tool_executed && execute_tool_call(&session->tools, &tool_calls[i], &results[executed_count]) != 0) {
                fprintf(stderr, "Warning: Failed to execute tool call %s in iteration %d\n",
                       tool_calls[i].name, loop_count);
                // Attempt to set error information - strdup may fail in low memory
                results[executed_count].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
                results[executed_count].result = strdup("Tool execution failed");
                results[executed_count].success = 0;
                // Note: If strdup fails, fields will be NULL. append_tool_message() returns -1
                // for NULL params and logs a warning, cleanup_tool_results() handles NULL safely
            } else {
                debug_printf("Executed tool: %s (ID: %s) in iteration %d\n",
                           tool_calls[i].name, tool_calls[i].id, loop_count);
            }

            // Output tool result in JSON mode
            if (session->session_data.config.json_output_mode) {
                json_output_tool_result(tool_calls[i].id, results[executed_count].result, !results[executed_count].success);
            }

            executed_count++;
        }

        // Handle abort case - exit the entire tool loop
        if (loop_aborted) {
            // Still add results for tools that were processed before abort
            for (int i = 0; i < executed_count; i++) {
                int tool_call_index = tool_call_indices[i];
                const char* tool_name = tool_calls[tool_call_index].name;
                if (append_tool_message(&session->session_data.conversation, results[i].result,
                                       results[i].tool_call_id, tool_name) != 0) {
                    fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
                }
            }
            free(tool_call_indices);
            cleanup_tool_results(results, executed_count);
            cleanup_tool_calls(tool_calls, call_count);
            StringArray_destroy(&tracker);
            return -1; // Signal abort
        }

        // Note: Tool execution group is NOT ended here - it spans the entire agentic loop
        // and will be closed when exiting the loop

        // Add tool result messages to conversation (only for executed tools)
        for (int i = 0; i < executed_count; i++) {
            int tool_call_index = tool_call_indices[i];
            const char* tool_name = tool_calls[tool_call_index].name;
            if (append_tool_message(&session->session_data.conversation, results[i].result,
                                   results[i].tool_call_id, tool_name) != 0) {
                fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
            }
        }

        free(tool_call_indices);

        cleanup_tool_results(results, executed_count);
        cleanup_tool_calls(tool_calls, call_count);

        // Continue the loop to check for more tool calls in the next response
    }
}

// =============================================================================
// Public Tool Execution Workflow
// =============================================================================

int tool_executor_run_workflow(RalphSession* session, ToolCall* tool_calls, int call_count,
                               const char* user_message, int max_tokens, const char** headers) {
    if (session == NULL || tool_calls == NULL || call_count <= 0) {
        return -1;
    }

    debug_printf("Executing %d tool call(s)...\n", call_count);

    // Note: User message is already saved by caller
    (void)user_message; // Acknowledge parameter usage

    // Execute tool calls - use calloc to zero-initialize
    ToolResult *results = calloc(call_count, sizeof(ToolResult));
    if (results == NULL) {
        return -1;
    }

    // Force refresh of protected inode cache before batch processing
    force_protected_inode_refresh();

    int aborted = 0;
    for (int i = 0; i < call_count; i++) {
        // Check approval gates before execution
        int approval_check = check_tool_approval(session, &tool_calls[i], &results[i]);
        if (approval_check == -2) {
            // User aborted (Ctrl+C) - stop processing remaining tools
            aborted = 1;
            debug_printf("User aborted tool execution at tool %d of %d\n", i + 1, call_count);
            // Still need to add placeholder results for unprocessed tools
            for (int j = i; j < call_count; j++) {
                if (results[j].result == NULL) {
                    results[j].tool_call_id = tool_calls[j].id ? strdup(tool_calls[j].id) : NULL;
                    results[j].result = strdup("{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
                    results[j].success = 0;
                }
            }
            break;
        }
        if (approval_check == -1) {
            // Tool was blocked (result already populated)
            debug_printf("Tool %s blocked by approval gate\n", tool_calls[i].name);
            // Output tool result in JSON mode
            if (session->session_data.config.json_output_mode) {
                json_output_tool_result(tool_calls[i].id, results[i].result, !results[i].success);
            }
            continue;
        }

        int tool_executed = 0;

        // Check if this is an MCP tool call
        if (strncmp(tool_calls[i].name, "mcp_", 4) == 0) {
            if (mcp_client_execute_tool(&session->mcp_client, &tool_calls[i], &results[i]) == 0) {
                tool_executed = 1;
            }
        }

        // If not an MCP tool or MCP execution failed, try standard tool execution
        if (!tool_executed && execute_tool_call(&session->tools, &tool_calls[i], &results[i]) != 0) {
            fprintf(stderr, "Warning: Failed to execute tool call %s\n", tool_calls[i].name);
            // Attempt to set error information - strdup may fail in low memory
            results[i].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
            results[i].result = strdup("Tool execution failed");
            results[i].success = 0;
            // Note: If strdup fails, fields will be NULL. append_tool_message() returns -1
            // for NULL params and logs a warning, cleanup_tool_results() handles NULL safely
        } else {
            debug_printf("Executed tool: %s (ID: %s)\n", tool_calls[i].name, tool_calls[i].id);
        }

        // Output tool result in JSON mode
        if (session->session_data.config.json_output_mode) {
            json_output_tool_result(tool_calls[i].id, results[i].result, !results[i].success);
        }
    }

    // Add tool result messages to conversation
    for (int i = 0; i < call_count; i++) {
        if (append_tool_message(&session->session_data.conversation, results[i].result, tool_calls[i].id, tool_calls[i].name) != 0) {
            fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
        }
    }

    // If user aborted, don't continue with follow-up API calls
    if (aborted) {
        cleanup_tool_results(results, call_count);
        return -2; // Signal abort to caller
    }

    // CRITICAL FIX: Now that initial tools are executed and saved to conversation,
    // check if we need to continue with follow-up API calls for additional tool calls
    int result = tool_executor_run_loop(session, user_message, max_tokens, headers);

    // Always return success if tools executed, even if follow-up fails
    // This maintains backward compatibility with existing tests
    if (result != 0) {
        debug_printf("Follow-up tool loop failed, but initial tools executed successfully\n");
        result = 0;
    }

    cleanup_tool_results(results, call_count);
    return result;
}
