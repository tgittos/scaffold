#include "recap.h"
#include "http_client.h"
#include "output_formatter.h"
#include "api_common.h"
#include "debug_output.h"
#include "token_manager.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Recap Generation (One-shot LLM call without history persistence)
// =============================================================================

#define RECAP_DEFAULT_MAX_MESSAGES 5
#define RECAP_INITIAL_BUFFER_SIZE 4096

// Helper function to format recent messages for recap context
static char* format_recent_messages_for_recap(const ConversationHistory* history, int max_messages) {
    if (history == NULL || history->count == 0) {
        return NULL;
    }

    // Determine how many messages to include
    int start_index = 0;
    int message_count = history->count;
    if (max_messages > 0 && message_count > max_messages) {
        start_index = message_count - max_messages;
        message_count = max_messages;
    }

    // Calculate buffer size needed (estimate)
    size_t buffer_size = RECAP_INITIAL_BUFFER_SIZE;
    for (int i = start_index; i < history->count; i++) {
        if (history->messages[i].content != NULL) {
            buffer_size += strlen(history->messages[i].content) + 64; // Extra for formatting
        }
    }

    char* buffer = malloc(buffer_size);
    if (buffer == NULL) {
        return NULL;
    }
    buffer[0] = '\0';

    // Format messages
    size_t offset = 0;
    for (int i = start_index; i < history->count; i++) {
        const ConversationMessage* msg = &history->messages[i];

        // Skip tool messages for cleaner recap
        if (msg->role != NULL && strcmp(msg->role, "tool") == 0) {
            continue;
        }

        const char* role = msg->role ? msg->role : "unknown";
        const char* content = msg->content ? msg->content : "";

        // Truncate very long messages for recap
        const int max_content_length = 500;
        char truncated_content[512];
        if (strlen(content) > (size_t)max_content_length) {
            strncpy(truncated_content, content, max_content_length - 3);
            truncated_content[max_content_length - 3] = '\0';
            strcat(truncated_content, "...");
            content = truncated_content;
        }

        int written = snprintf(buffer + offset, buffer_size - offset,
                              "**%s**: %s\n\n", role, content);
        if (written < 0 || (size_t)written >= buffer_size - offset) {
            break; // Buffer full
        }
        offset += written;
    }

    return buffer;
}

// Generate a recap of recent conversation without persisting to history
int ralph_generate_recap(RalphSession* session, int max_messages) {
    if (session == NULL) {
        return -1;
    }

    const ConversationHistory* history = &session->session_data.conversation;
    if (history->count == 0) {
        return 0; // Nothing to recap
    }

    // Use default if not specified
    if (max_messages <= 0) {
        max_messages = RECAP_DEFAULT_MAX_MESSAGES;
    }

    // Format recent messages
    char* recent_messages = format_recent_messages_for_recap(history, max_messages);
    if (recent_messages == NULL) {
        return -1;
    }

    // Build the recap prompt
    const char* recap_template =
        "You are resuming a conversation. Here are the most recent messages:\n\n"
        "%s\n"
        "Please provide a very brief recap (2-3 sentences max) of what was being discussed, "
        "and ask how you can continue to help. Be warm and conversational.";

    size_t prompt_size = strlen(recap_template) + strlen(recent_messages) + 1;
    char* recap_prompt = malloc(prompt_size);
    if (recap_prompt == NULL) {
        free(recent_messages);
        return -1;
    }

    snprintf(recap_prompt, prompt_size, recap_template, recent_messages);
    free(recent_messages);

    debug_printf("Generating recap with prompt: %s\n", recap_prompt);

    // Build minimal JSON payload (no conversation history - just the recap prompt)
    // We use an empty conversation to avoid the context being duplicated
    ConversationHistory empty_history = {0};

    // Get token allocation for the recap request
    TokenConfig token_config;
    token_config_init(&token_config, session->session_data.config.context_window);

    // Use a reasonable max_tokens for a short recap response
    int max_tokens = 300;

    // Build the API payload
    char* post_data = NULL;
    if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
        post_data = ralph_build_anthropic_json_payload(
            session->session_data.config.model,
            session->session_data.config.system_prompt,
            &empty_history,
            recap_prompt,
            max_tokens,
            NULL  // No tools for recap
        );
    } else {
        post_data = ralph_build_json_payload(
            session->session_data.config.model,
            session->session_data.config.system_prompt,
            &empty_history,
            recap_prompt,
            session->session_data.config.max_tokens_param,
            max_tokens,
            NULL  // No tools for recap
        );
    }

    free(recap_prompt);

    if (post_data == NULL) {
        fprintf(stderr, "Error: Failed to build recap JSON payload\n");
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
            int ret = snprintf(auth_header, sizeof(auth_header), "x-api-key: %s",
                             session->session_data.config.api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
            headers[header_count++] = anthropic_version;
            headers[header_count++] = content_type;
        } else {
            int ret = snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
                             session->session_data.config.api_key);
            if (ret < 0 || ret >= (int)sizeof(auth_header)) {
                free(post_data);
                return -1;
            }
            headers[header_count++] = auth_header;
        }
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    debug_printf("Making recap API request to %s\n", session->session_data.config.api_url);
    debug_printf("POST data: %s\n\n", post_data);

    // Display thinking indicator
    if (!session->session_data.config.json_output_mode) {
        fprintf(stdout, "\033[36m•\033[0m ");
        fflush(stdout);
    }

    struct HTTPResponse response = {0};
    int result = -1;

    if (http_post_with_headers(session->session_data.config.api_url, post_data, headers, &response) == 0) {
        if (response.data == NULL) {
            fprintf(stderr, "Error: Empty response from API\n");
            cleanup_response(&response);
            free(post_data);
            curl_global_cleanup();
            return -1;
        }

        // Parse the response
        ParsedResponse parsed_response;
        int parse_result;
        if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
            parse_result = parse_anthropic_response(response.data, &parsed_response);
        } else {
            parse_result = parse_api_response(response.data, &parsed_response);
        }

        if (parse_result == 0) {
            // Clear the thinking indicator
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, "\r\033[K");
                fflush(stdout);
            }

            // Display the recap response (using the standard formatter)
            print_formatted_response_improved(&parsed_response);
            result = 0;

            cleanup_parsed_response(&parsed_response);
        } else {
            // Clear thinking indicator on error
            if (!session->session_data.config.json_output_mode) {
                fprintf(stdout, "\r\033[K");
                fflush(stdout);
            }
            fprintf(stderr, "Error: Failed to parse recap response\n");
        }
    } else {
        // Clear thinking indicator on error
        if (!session->session_data.config.json_output_mode) {
            fprintf(stdout, "\r\033[K");
            fflush(stdout);
        }
        // Log the actual response if we got one (might contain error details)
        if (response.data != NULL && response.size > 0) {
            debug_printf("Recap API error response: %s\n", response.data);
        }
        fprintf(stderr, "Recap API request failed\n");
    }

    cleanup_response(&response);
    free(post_data);
    curl_global_cleanup();

    // NOTE: We intentionally do NOT save the recap exchange to conversation history
    // This keeps the history clean and avoids bloating it with recap prompts

    return result;
}
