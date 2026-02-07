#include "api_round_trip.h"
#include "../util/debug_output.h"
#include "../network/api_error.h"
#include "../llm/llm_client.h"
#include "../llm/model_capabilities.h"
#include "../network/http_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int api_round_trip_execute(AgentSession* session, const char* user_message,
                           int max_tokens, LLMRoundTripResult* result) {
    if (session == NULL || result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(*result));

    char* post_data = NULL;
    if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
        post_data = session_build_anthropic_json_payload(session, user_message, max_tokens);
    } else {
        post_data = session_build_json_payload(session, user_message, max_tokens);
    }

    if (post_data == NULL) {
        return -1;
    }

    if (!session->session_data.config.json_output_mode) {
        fprintf(stdout, TERM_CYAN TERM_SYM_ACTIVE TERM_RESET " ");
        fflush(stdout);
    }

    struct HTTPResponse response = {0};

    if (llm_client_send(session->session_data.config.api_url,
                        session->session_data.config.api_key,
                        post_data, &response) != 0) {
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
        fprintf(stderr, "Error: Empty response from API\n");
        cleanup_response(&response);
        free(post_data);
        return -1;
    }

    int parse_result;
    if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
        parse_result = parse_anthropic_response(response.data, &result->parsed);
    } else {
        parse_result = parse_api_response(response.data, &result->parsed);
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
            fprintf(stderr, "API request failed.\n");
            if (debug_enabled) {
                fprintf(stderr, "Debug: %s\n", response.data);
            }
        } else {
            fprintf(stderr, "Error: Failed to parse API response\n");
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

    const char* content = result->parsed.response_content ?
                          result->parsed.response_content :
                          result->parsed.thinking_content;

    int tool_parse_result = parse_model_tool_calls(
        session->model_registry, session->session_data.config.model,
        response.data, &result->tool_calls, &result->tool_call_count);

    if (tool_parse_result != 0 || result->tool_call_count == 0) {
        if (content != NULL &&
            parse_model_tool_calls(session->model_registry,
                                   session->session_data.config.model,
                                   content, &result->tool_calls,
                                   &result->tool_call_count) == 0 &&
            result->tool_call_count > 0) {
            debug_printf("Found %d tool calls in message content (custom format)\n",
                        result->tool_call_count);
        }
    }

    cleanup_response(&response);
    free(post_data);
    return 0;
}

void api_round_trip_cleanup(LLMRoundTripResult* result) {
    if (result == NULL) {
        return;
    }

    cleanup_parsed_response(&result->parsed);

    if (result->tool_calls != NULL) {
        cleanup_tool_calls(result->tool_calls, result->tool_call_count);
        result->tool_calls = NULL;
        result->tool_call_count = 0;
    }
}
