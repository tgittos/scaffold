#include "message_processor.h"
#include "conversation_state.h"
#include "tool_executor.h"
#include "../tools/tools_system.h"
#include "../ui/output_formatter.h"
#include "../ui/json_output.h"
#include "../util/debug_output.h"
#include "../plugin/hook_dispatcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int message_processor_handle_response(AgentSession* session,
                                      LLMRoundTripResult* result,
                                      const char* user_message,
                                      int max_tokens) {
    if (session == NULL || result == NULL) return -1;

    const char* message_content = result->parsed.response_content ?
                                  result->parsed.response_content :
                                  result->parsed.thinking_content;

    /* Take ownership of tool calls from the round-trip result */
    ToolCall* tool_calls = result->tool_calls;
    int call_count = result->tool_call_count;
    result->tool_calls = NULL;
    result->tool_call_count = 0;

    /* Fallback: generic parser for models without a registry-level parser */
    if (call_count == 0 && message_content != NULL) {
        if (parse_tool_calls(message_content, &tool_calls, &call_count) == 0 && call_count > 0) {
            debug_printf("Found %d tool calls via generic parser fallback\n", call_count);
        }
    }

    /* Plugin hook: post_llm_response */
    char *hook_text = message_content ? strdup(message_content) : NULL;
    hook_dispatch_post_llm_response(&session->plugin_manager, session,
                                     &hook_text, tool_calls, call_count);
    /* Use hook_text for display if plugins transformed it */
    if (hook_text && message_content &&
        strcmp(hook_text, message_content) != 0) {
        /* Plugin modified text; update the appropriate parsed field */
        if (result->parsed.response_content) {
            free(result->parsed.response_content);
        } else {
            /* Was using thinking_content as fallback; clear stale copy */
            free(result->parsed.thinking_content);
            result->parsed.thinking_content = NULL;
        }
        result->parsed.response_content = strdup(hook_text);
        message_content = result->parsed.response_content;
    }
    free(hook_text);

    print_formatted_response_improved(&result->parsed);

    if (user_message != NULL && strlen(user_message) > 0) {
        if (append_conversation_message(&session->session_data.conversation, "user", user_message) != 0) {
            fprintf(stderr, "Warning: Failed to save user message to conversation history\n");
        }
    }

    int rc;
    if (call_count > 0) {
        debug_printf("Found %d tool calls in response\n", call_count);

        if (session->session_data.config.json_output_mode) {
            json_output_assistant_tool_calls_buffered(tool_calls, call_count,
                                                      result->parsed.prompt_tokens,
                                                      result->parsed.completion_tokens);
        }

        conversation_append_assistant(session, message_content, tool_calls, call_count);
        rc = session_execute_tool_workflow(session, tool_calls, call_count, user_message, max_tokens);
        cleanup_tool_calls(tool_calls, call_count);
    } else {
        conversation_append_assistant(session, message_content, NULL, 0);

        if (message_content != NULL && session->session_data.config.json_output_mode) {
            json_output_assistant_text(message_content,
                                       result->parsed.prompt_tokens,
                                       result->parsed.completion_tokens);
        }
        rc = 0;
    }

    return rc;
}
