#include "tool_executor.h"
#include "session.h"
#include "tool_orchestration.h"
#include "../util/interrupt.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include <cJSON.h>
#include "../util/debug_output.h"
#include "../network/api_error.h"
#include "../llm/llm_client.h"
#include "../session/token_manager.h"
#include "../llm/model_capabilities.h"
#include "../mcp/mcp_client.h"
#include "../ui/spinner.h"
#include "../policy/protected_files.h"
#include "../policy/verified_file_context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../util/json_escape.h"

char* construct_openai_assistant_message_with_tools(const char* content,
                                                    const ToolCall* tool_calls,
                                                    int call_count) {
    if (call_count <= 0 || tool_calls == NULL) {
        return content ? strdup(content) : NULL;
    }

    size_t base_size = 200;
    size_t content_len = content ? strlen(content) : 0;

    if (content_len > SIZE_MAX / 2 - 50) {
        return NULL;
    }
    size_t content_size = content_len * 2 + 50; // worst-case JSON escaping

    if (call_count > 0 && (size_t)call_count > SIZE_MAX / 200) {
        return NULL;
    }
    size_t tools_size = (size_t)call_count * 200;

    if (base_size > SIZE_MAX - content_size ||
        base_size + content_size > SIZE_MAX - tools_size) {
        return NULL;
    }

    char* message = malloc(base_size + content_size + tools_size);
    if (message == NULL) {
        return NULL;
    }

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

    for (int i = 0; i < call_count; i++) {
        char* escaped_args = json_escape_string(tool_calls[i].arguments ? tool_calls[i].arguments : "{}");
        if (escaped_args == NULL) {
            free(message);
            return NULL;
        }

        const char* id = tool_calls[i].id ? tool_calls[i].id : "";
        const char* name = tool_calls[i].name ? tool_calls[i].name : "";
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

static int tool_executor_run_loop(AgentSession* session, const char* user_message,
                                  int max_tokens, ToolOrchestrationContext* ctx) {
    if (session == NULL || ctx == NULL) {
        return -1;
    }

    (void)user_message;
    (void)max_tokens;

    int loop_count = 0;

    debug_printf("Starting iterative tool calling loop\n");

    while (1) {
        loop_count++;
        tool_orchestration_reset_batch(ctx);
        debug_printf("Tool calling loop iteration %d\n", loop_count);

        TokenConfig token_config;
        token_config_init(&token_config, session->session_data.config.context_window);
        TokenUsage token_usage;
        if (manage_conversation_tokens(session, "", &token_config, &token_usage) != 0) {
            fprintf(stderr, "Error: Failed to calculate token allocation for tool loop iteration %d\n", loop_count);
            return -1;
        }

        int iteration_max_tokens = token_usage.available_response_tokens;
        debug_printf("Using %d max_tokens for tool loop iteration %d\n", iteration_max_tokens, loop_count);

        char* post_data = NULL;
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            post_data = session_build_anthropic_json_payload(session, "", iteration_max_tokens);
        } else {
            post_data = session_build_json_payload(session, "", iteration_max_tokens);
        }

        if (post_data == NULL) {
            fprintf(stderr, "Error: Failed to build JSON payload for tool loop iteration %d\n", loop_count);
            return -1;
        }

        struct HTTPResponse response = {0};
        debug_printf("Making API request for tool loop iteration %d\n", loop_count);

        if (!session->session_data.config.json_output_mode) {
            fprintf(stdout, TERM_CYAN TERM_SYM_ACTIVE TERM_RESET " ");
            fflush(stdout);
        }

        if (llm_client_send(session->session_data.config.api_url, session->session_data.config.api_key, post_data, &response) != 0) {
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, TERM_CLEAR_LINE);
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
            return -1;
        }

        if (response.data == NULL) {
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, TERM_CLEAR_LINE);
                fflush(stdout);
            }
            fprintf(stderr, "Error: Empty response from API in tool loop iteration %d\n", loop_count);
            cleanup_response(&response);
            free(post_data);
            return -1;
        }

        ParsedResponse parsed_response;
        int parse_result;
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            parse_result = parse_anthropic_response(response.data, &parsed_response);
        } else {
            parse_result = parse_api_response(response.data, &parsed_response);
        }

        if (parse_result != 0) {
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, TERM_CLEAR_LINE);
                fflush(stdout);
            }

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
            return -1;
        }

        if (!session->session_data.config.json_output_mode) {
            fprintf(stdout, TERM_CLEAR_LINE);
            fflush(stdout);
        }

        // Response display is deferred until we know whether this iteration has tool calls
        ToolCall *tool_calls = NULL;
        int call_count = 0;
        int tool_parse_result;

        tool_parse_result = parse_model_tool_calls(session->model_registry, session->session_data.config.model,
                                                  response.data, &tool_calls, &call_count);

        const char* assistant_content = parsed_response.response_content ?
                                       parsed_response.response_content :
                                       parsed_response.thinking_content;
        // Some models embed tool calls in message content rather than the standard location
        if (tool_parse_result != 0 || call_count == 0) {
            if (assistant_content != NULL && parse_model_tool_calls(session->model_registry, session->session_data.config.model,
                                                                    assistant_content, &tool_calls, &call_count) == 0 && call_count > 0) {
                tool_parse_result = 0;
                debug_printf("Found %d tool calls in message content (custom format)\n", call_count);
            }
        }

        if (tool_parse_result == 0 && call_count > 0) {
            // Display text content before tool execution so reasoning appears interleaved
            if (parsed_response.response_content != NULL && strlen(parsed_response.response_content) > 0) {
                if (!session->session_data.config.json_output_mode) {
                    printf("%s\n", parsed_response.response_content);
                    fflush(stdout);
                } else {
                    json_output_assistant_text(parsed_response.response_content,
                                               parsed_response.prompt_tokens,
                                               parsed_response.completion_tokens);
                }
            }

            if (session->session_data.config.json_output_mode) {
                json_output_assistant_tool_calls_buffered(tool_calls, call_count,
                                                          parsed_response.prompt_tokens,
                                                          parsed_response.completion_tokens);
            }

            // Use parsed content, not raw response.data which includes the full API envelope
            char* formatted_message = format_model_assistant_tool_message(session->model_registry,
                                                                         session->session_data.config.model,
                                                                         assistant_content, tool_calls, call_count);
            if (formatted_message) {
                if (append_conversation_message(&session->session_data.conversation, "assistant", formatted_message) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response with tool calls to conversation history\n");
                }
                free(formatted_message);
            } else {
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
                    if (append_conversation_message(&session->session_data.conversation, "assistant", "Executed tool calls") != 0) {
                        fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                    }
                }
            }
        } else {
            if (assistant_content != NULL) {
                if (append_conversation_message(&session->session_data.conversation, "assistant", assistant_content) != 0) {
                    fprintf(stderr, "Warning: Failed to save assistant response to conversation history\n");
                }

                if (session->session_data.config.json_output_mode) {
                    json_output_assistant_text(assistant_content,
                                               parsed_response.prompt_tokens,
                                               parsed_response.completion_tokens);
                }
            }
        }

        cleanup_response(&response);
        free(post_data);

        if (tool_parse_result != 0 || call_count == 0) {
            debug_printf("No more tool calls found - ending tool loop after %d iterations\n", loop_count);
            print_formatted_response_improved(&parsed_response);
            cleanup_parsed_response(&parsed_response);
            return 0;
        }

        cleanup_parsed_response(&parsed_response);

        // Deduplicate to prevent infinite loops when the LLM re-emits the same tool call IDs
        int new_tool_calls = 0;
        for (int i = 0; i < call_count; i++) {
            if (!tool_orchestration_is_duplicate(ctx, tool_calls[i].id)) {
                new_tool_calls++;
            }
        }

        if (new_tool_calls == 0) {
            debug_printf("All %d tool calls already executed - ending loop to prevent infinite iteration\n", call_count);
            cleanup_tool_calls(tool_calls, call_count);
            return 0;
        }

        debug_printf("Found %d new tool calls (out of %d total) in iteration %d - executing them\n",
                    new_tool_calls, call_count, loop_count);

        ToolResult *results = calloc(call_count, sizeof(ToolResult));
        if (results == NULL) {
            cleanup_tool_calls(tool_calls, call_count);
            return -1;
        }

        int *tool_call_indices = malloc(call_count * sizeof(int));
        if (tool_call_indices == NULL) {
            free(results);
            cleanup_tool_calls(tool_calls, call_count);
            return -1;
        }

        force_protected_inode_refresh();

        int executed_count = 0;
        int loop_aborted = 0;
        int loop_interrupted = 0;
        for (int i = 0; i < call_count; i++) {
            if (interrupt_pending()) {
                interrupt_acknowledge();
                spinner_stop();
                loop_interrupted = 1;
                debug_printf("Tool execution interrupted by user at tool %d of %d\n", i + 1, call_count);
                display_cancellation_message(i, call_count, session->session_data.config.json_output_mode);
                for (int j = i; j < call_count; j++) {
                    if (!tool_orchestration_is_duplicate(ctx, tool_calls[j].id)) {
                        results[executed_count].tool_call_id = tool_calls[j].id ? strdup(tool_calls[j].id) : NULL;
                        results[executed_count].result = strdup("{\"error\": \"interrupted\", \"message\": \"Cancelled by user\"}");
                        results[executed_count].success = 0;
                        tool_call_indices[executed_count] = j;
                        if (session->session_data.config.json_output_mode) {
                            json_output_tool_result(tool_calls[j].id, results[executed_count].result, 1);
                        }
                        executed_count++;
                    }
                }
                break;
            }

            if (tool_orchestration_is_duplicate(ctx, tool_calls[i].id)) {
                debug_printf("Skipping already executed tool: %s (ID: %s)\n",
                           tool_calls[i].name, tool_calls[i].id);
                continue;
            }

            // Skip execution if tracking fails to prevent duplicate runs in later iterations
            if (tool_orchestration_mark_executed(ctx, tool_calls[i].id) != 0) {
                debug_printf("Warning: Failed to track tool call ID %s, skipping execution\n", tool_calls[i].id);
                continue;
            }

            tool_call_indices[executed_count] = i;

            if (!tool_orchestration_can_spawn_subagent(ctx, tool_calls[i].name)) {
                debug_printf("Skipping duplicate subagent call %d in loop iteration %d (ID: %s)\n",
                             i, loop_count, tool_calls[i].id);
                results[executed_count].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
                results[executed_count].result = strdup("{\"error\": \"duplicate_subagent\", \"message\": "
                                                        "\"Only one subagent can be spawned per turn. "
                                                        "A subagent was already spawned in this batch.\"}");
                results[executed_count].success = 0;
                if (!session->session_data.config.json_output_mode) {
                    log_tool_execution_improved(tool_calls[i].name, tool_calls[i].arguments, 0,
                                                "Duplicate subagent blocked");
                } else {
                    json_output_tool_result(tool_calls[i].id, results[executed_count].result, 1);
                }
                executed_count++;
                continue;
            } else if (strcmp(tool_calls[i].name, "subagent") == 0) {
                debug_printf("First subagent call in loop iteration %d (ID: %s)\n", loop_count, tool_calls[i].id);
            }

            int approval_check = tool_orchestration_check_approval(ctx, &tool_calls[i], &results[executed_count]);
            if (approval_check == -2) {
                loop_aborted = 1;
                debug_printf("User aborted tool execution in loop iteration %d\n", loop_count);
                results[executed_count].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
                results[executed_count].result = strdup("{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
                results[executed_count].success = 0;
                log_tool_execution_improved(tool_calls[i].name, tool_calls[i].arguments, 0, "Aborted by user");
                executed_count++;
                break;
            }
            if (approval_check == -1) {
                debug_printf("Tool %s blocked by approval gate in iteration %d\n",
                           tool_calls[i].name, loop_count);
                if (session->session_data.config.json_output_mode) {
                    json_output_tool_result(tool_calls[i].id, results[executed_count].result, !results[executed_count].success);
                }
                executed_count++;
                continue;
            }

            int tool_executed = 0;

            spinner_start(tool_calls[i].name, tool_calls[i].arguments);

            if (strncmp(tool_calls[i].name, "mcp_", 4) == 0) {
                if (mcp_client_execute_tool(&session->mcp_client, &tool_calls[i], &results[executed_count]) == 0) {
                    tool_executed = 1;
                }
            }

            if (!tool_executed && execute_tool_call(&session->tools, &tool_calls[i], &results[executed_count]) != 0) {
                fprintf(stderr, "Warning: Failed to execute tool call %s in iteration %d\n",
                       tool_calls[i].name, loop_count);
                results[executed_count].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
                results[executed_count].result = strdup("Tool execution failed");
                results[executed_count].success = 0;
            } else {
                debug_printf("Executed tool: %s (ID: %s) in iteration %d\n",
                           tool_calls[i].name, tool_calls[i].id, loop_count);
            }

            spinner_stop();

            /* Log tool result after spinner is cleared */
            if (!session->session_data.config.json_output_mode) {
                log_tool_execution_improved(tool_calls[i].name, tool_calls[i].arguments,
                                            results[executed_count].success, results[executed_count].result);
            }

            // Clear per-tool file context to prevent leaking to subsequent calls
            verified_file_context_clear();

            if (session->session_data.config.json_output_mode) {
                json_output_tool_result(tool_calls[i].id, results[executed_count].result, !results[executed_count].success);
            }

            executed_count++;
        }

        if (loop_aborted || loop_interrupted) {
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
            return loop_interrupted ? -2 : -1;
        }

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
    }
}

int tool_executor_run_workflow(AgentSession* session, ToolCall* tool_calls, int call_count,
                               const char* user_message, int max_tokens) {
    if (session == NULL || tool_calls == NULL || call_count <= 0) {
        return -1;
    }

    debug_printf("Executing %d tool call(s)...\n", call_count);

    (void)user_message; // Saved by caller; passed through for the follow-up loop

    ToolOrchestrationContext ctx;
    if (tool_orchestration_init(&ctx, &session->gate_config) != 0) {
        return -1;
    }

    ToolResult *results = calloc(call_count, sizeof(ToolResult));
    if (results == NULL) {
        tool_orchestration_cleanup(&ctx);
        return -1;
    }

    force_protected_inode_refresh();

    int aborted = 0;
    int interrupted = 0;
    for (int i = 0; i < call_count; i++) {
        if (interrupt_pending()) {
            interrupt_acknowledge();
            spinner_stop();
            interrupted = 1;
            debug_printf("Tool workflow interrupted by user at tool %d of %d\n", i + 1, call_count);
            display_cancellation_message(i, call_count, session->session_data.config.json_output_mode);
            for (int j = i; j < call_count; j++) {
                results[j].tool_call_id = tool_calls[j].id ? strdup(tool_calls[j].id) : NULL;
                results[j].result = strdup("{\"error\": \"interrupted\", \"message\": \"Cancelled by user\"}");
                results[j].success = 0;
                if (session->session_data.config.json_output_mode) {
                    json_output_tool_result(tool_calls[j].id, results[j].result, 1);
                }
            }
            break;
        }

        if (!tool_orchestration_can_spawn_subagent(&ctx, tool_calls[i].name)) {
            debug_printf("Skipping duplicate subagent call %d in batch (ID: %s)\n",
                         i, tool_calls[i].id);
            results[i].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
            results[i].result = strdup("{\"error\": \"duplicate_subagent\", \"message\": "
                                       "\"Only one subagent can be spawned per turn. "
                                       "A subagent was already spawned in this batch.\"}");
            results[i].success = 0;
            if (!session->session_data.config.json_output_mode) {
                log_tool_execution_improved(tool_calls[i].name, tool_calls[i].arguments, 0,
                                            "Duplicate subagent blocked");
            } else {
                json_output_tool_result(tool_calls[i].id, results[i].result, 1);
            }
            continue;
        } else if (strcmp(tool_calls[i].name, "subagent") == 0) {
            debug_printf("First subagent call in batch (ID: %s)\n", tool_calls[i].id);
        }

        int approval_check = tool_orchestration_check_approval(&ctx, &tool_calls[i], &results[i]);
        if (approval_check == -2) {
            aborted = 1;
            debug_printf("User aborted tool execution at tool %d of %d\n", i + 1, call_count);
            results[i].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
            results[i].result = strdup("{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
            results[i].success = 0;
            log_tool_execution_improved(tool_calls[i].name, tool_calls[i].arguments, 0, "Aborted by user");
            for (int j = i + 1; j < call_count; j++) {
                results[j].tool_call_id = tool_calls[j].id ? strdup(tool_calls[j].id) : NULL;
                results[j].result = strdup("{\"error\": \"aborted\", \"message\": \"Operation aborted by user\"}");
                results[j].success = 0;
            }
            break;
        }
        if (approval_check == -1) {
            debug_printf("Tool %s blocked by approval gate\n", tool_calls[i].name);
            if (session->session_data.config.json_output_mode) {
                json_output_tool_result(tool_calls[i].id, results[i].result, !results[i].success);
            }
            continue;
        }

        int tool_executed = 0;

        spinner_start(tool_calls[i].name, tool_calls[i].arguments);

        if (strncmp(tool_calls[i].name, "mcp_", 4) == 0) {
            if (mcp_client_execute_tool(&session->mcp_client, &tool_calls[i], &results[i]) == 0) {
                tool_executed = 1;
            }
        }

        if (!tool_executed && execute_tool_call(&session->tools, &tool_calls[i], &results[i]) != 0) {
            fprintf(stderr, "Warning: Failed to execute tool call %s\n", tool_calls[i].name);
            results[i].tool_call_id = tool_calls[i].id ? strdup(tool_calls[i].id) : NULL;
            results[i].result = strdup("Tool execution failed");
            results[i].success = 0;
        } else {
            debug_printf("Executed tool: %s (ID: %s)\n", tool_calls[i].name, tool_calls[i].id);
        }

        spinner_stop();

        /* Log tool result after spinner is cleared */
        if (!session->session_data.config.json_output_mode) {
            log_tool_execution_improved(tool_calls[i].name, tool_calls[i].arguments,
                                        results[i].success, results[i].result);
        }

        verified_file_context_clear();

        if (session->session_data.config.json_output_mode) {
            json_output_tool_result(tool_calls[i].id, results[i].result, !results[i].success);
        }
    }

    for (int i = 0; i < call_count; i++) {
        if (append_tool_message(&session->session_data.conversation, results[i].result, tool_calls[i].id, tool_calls[i].name) != 0) {
            fprintf(stderr, "Warning: Failed to save tool result to conversation history\n");
        }
    }

    if (aborted || interrupted) {
        cleanup_tool_results(results, call_count);
        tool_orchestration_cleanup(&ctx);
        return -2;
    }

    // Seed the tracker with IDs from the initial batch so the follow-up loop
    // can detect re-emitted IDs and avoid duplicate execution.
    for (int i = 0; i < call_count; i++) {
        tool_orchestration_mark_executed(&ctx, tool_calls[i].id);
    }

    // Continue the agentic loop: the LLM may request additional tool calls
    int result = tool_executor_run_loop(session, user_message, max_tokens, &ctx);

    // Treat follow-up loop failure as non-fatal since initial tools already executed
    if (result != 0) {
        debug_printf("Follow-up tool loop failed, but initial tools executed successfully\n");
        result = 0;
    }

    cleanup_tool_results(results, call_count);
    tool_orchestration_cleanup(&ctx);
    return result;
}
