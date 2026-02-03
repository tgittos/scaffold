#include "conversation_compactor.h"
#include "rolling_summary.h"
#include "token_manager.h"
#include "util/debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PRESERVE_RECENT_MESSAGES 10
#define ESTIMATED_TOKENS_PER_MESSAGE 50
#define MIN_MESSAGES_FOR_SUMMARY 3

void compaction_config_init(CompactionConfig* config) {
    if (config == NULL) return;
    
    config->preserve_recent_messages = DEFAULT_PRESERVE_RECENT_MESSAGES;
    /* Default 8K context; callers with larger windows should reconfigure */
    config->background_threshold = (int)(8192 * COMPACTION_TRIGGER_THRESHOLD);
}

int should_background_compact(const ConversationHistory* conversation,
                             const CompactionConfig* config,
                             int current_token_count) {
    if (conversation == NULL || config == NULL) {
        return 0;
    }
    
    if (conversation->count <= (size_t)config->preserve_recent_messages + 5) {
        return 0;
    }
    
    if (current_token_count >= config->background_threshold) {
        debug_printf("Background trimming triggered: %d tokens >= %d threshold\n", 
                    current_token_count, config->background_threshold);
        return 1;
    }
    
    return 0;
}

static int estimate_messages_to_trim(const ConversationHistory* conversation,
                                     const TokenConfig* token_config,
                                     int current_tokens,
                                     int target_tokens,
                                     const char* system_prompt) {
    if (conversation == NULL || token_config == NULL) return 0;
    if (current_tokens <= target_tokens) return 0;

    int tokens_to_remove = current_tokens - target_tokens;
    int system_tokens = system_prompt ? estimate_token_count(system_prompt, token_config) : 0;
    int running_tokens = system_tokens;
    int messages_to_trim = 0;

    for (size_t i = 0; i < conversation->count && running_tokens < tokens_to_remove; i++) {
        if (conversation->data[i].role != NULL &&
            strcmp(conversation->data[i].role, "tool") != 0) {
            running_tokens += estimate_token_count(conversation->data[i].content, token_config);
            running_tokens += 10;
            messages_to_trim++;
        }
    }

    return messages_to_trim;
}

int background_compact_conversation(SessionData* session,
                                   const CompactionConfig* config,
                                   CompactionResult* result) {
    if (session == NULL || config == NULL || result == NULL) {
        return COMPACT_ERROR;
    }

    memset(result, 0, sizeof(CompactionResult));

    TokenConfig token_config;
    token_config_init(&token_config, session->config.context_window);

    int current_tokens = 0;
    for (size_t i = 0; i < session->conversation.count; i++) {
        current_tokens += estimate_token_count(session->conversation.data[i].content, &token_config);
    }

    if (!should_background_compact(&session->conversation, config, current_tokens)) {
        return COMPACT_SUCCESS_NO_WORK;
    }

    debug_printf("Background conversation trimming (%d tokens >= %d threshold)\n",
                current_tokens, config->background_threshold);
    debug_printf("Removing oldest messages to maintain performance (full history preserved in vector DB)\n");

    int target_tokens = (int)(session->config.context_window * COMPACTION_TARGET_THRESHOLD);

    int messages_to_trim = estimate_messages_to_trim(
        &session->conversation,
        &token_config,
        current_tokens,
        target_tokens,
        session->config.system_prompt
    );

    if (messages_to_trim >= MIN_MESSAGES_FOR_SUMMARY &&
        session->config.api_url != NULL &&
        session->config.model != NULL) {
        debug_printf("Generating rolling summary for %d messages before trimming\n", messages_to_trim);

        char* new_summary = NULL;
        int summary_result = generate_rolling_summary(
            session->config.api_url,
            session->config.api_key,
            session->config.api_type,
            session->config.model,
            session->conversation.data,
            messages_to_trim,
            session->rolling_summary.summary_text,
            &new_summary
        );

        if (summary_result == 0 && new_summary != NULL) {
            free(session->rolling_summary.summary_text);
            session->rolling_summary.summary_text = new_summary;
            session->rolling_summary.messages_summarized += messages_to_trim;

            TokenConfig sum_token_config;
            token_config_init(&sum_token_config, session->config.context_window);
            session->rolling_summary.estimated_tokens =
                estimate_token_count(new_summary, &sum_token_config);

            debug_printf("Rolling summary updated (%d total messages summarized, ~%d tokens)\n",
                        session->rolling_summary.messages_summarized,
                        session->rolling_summary.estimated_tokens);
        } else {
            debug_printf("Failed to generate rolling summary, proceeding with trim anyway\n");
        }
    }

    int messages_trimmed = trim_conversation_for_tokens(&session->conversation, &token_config, target_tokens, session->config.system_prompt);

    if (messages_trimmed > 0) {
        int tokens_saved = messages_trimmed * ESTIMATED_TOKENS_PER_MESSAGE;

        result->messages_trimmed = messages_trimmed;
        result->messages_after_trimming = session->conversation.count;
        result->tokens_saved = tokens_saved;

        debug_printf("Background trimming complete: removed %d messages (saved ~%d tokens)\n",
                    messages_trimmed, tokens_saved);
        debug_printf("Full conversation history remains available in vector database\n");

        debug_printf("Background trimming: removed %d messages, %d remaining\n",
                    messages_trimmed, (int)session->conversation.count);

        return COMPACT_SUCCESS_TRIMMED;
    }

    return COMPACT_SUCCESS_NO_WORK;
}

int compact_conversation(SessionData* session,
                        const CompactionConfig* config,
                        int target_token_count,
                        CompactionResult* result) {
    if (session == NULL || config == NULL || result == NULL) {
        return COMPACT_ERROR;
    }

    memset(result, 0, sizeof(CompactionResult));

    TokenConfig token_config;
    token_config_init(&token_config, session->config.context_window);

    int current_tokens = 0;
    for (size_t i = 0; i < session->conversation.count; i++) {
        current_tokens += estimate_token_count(session->conversation.data[i].content, &token_config);
    }

    if (current_tokens <= target_token_count) {
        return COMPACT_SUCCESS_NO_WORK;
    }

    debug_printf("Context window approaching limit (%d/%d tokens)\n", current_tokens, target_token_count);
    debug_printf("Trimming conversation to maintain performance (full history preserved in vector DB)\n");

    int messages_trimmed = trim_conversation_for_tokens(&session->conversation, &token_config, target_token_count, session->config.system_prompt);

    if (messages_trimmed > 0) {
        int tokens_saved = messages_trimmed * ESTIMATED_TOKENS_PER_MESSAGE;

        result->messages_trimmed = messages_trimmed;
        result->messages_after_trimming = session->conversation.count;
        result->tokens_saved = tokens_saved;

        debug_printf("Emergency trimming complete: removed %d messages (saved ~%d tokens)\n",
                    messages_trimmed, tokens_saved);
        debug_printf("Full conversation history remains available in vector database\n");

        debug_printf("Emergency trimming: removed %d messages, %d remaining\n",
                    messages_trimmed, session->conversation.count);

        return COMPACT_SUCCESS_TRIMMED;
    }

    return COMPACT_SUCCESS_NO_WORK;
}

void cleanup_compaction_result(CompactionResult* result) {
    if (result == NULL) {
        return;
    }
    
    memset(result, 0, sizeof(CompactionResult));
}