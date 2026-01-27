#include "conversation_compactor.h"
#include "token_manager.h"
#include "debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_PRESERVE_RECENT_MESSAGES 10
#define DEFAULT_BACKGROUND_THRESHOLD 0.6f
#define ESTIMATED_TOKENS_PER_MESSAGE 50

void compaction_config_init(CompactionConfig* config) {
    if (config == NULL) return;
    
    config->preserve_recent_messages = DEFAULT_PRESERVE_RECENT_MESSAGES;
    // Default assumes 8K context; callers with larger windows should reconfigure
    config->background_threshold = (int)(8192 * DEFAULT_BACKGROUND_THRESHOLD);
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

    // Target 50% to leave headroom before the next compaction is needed
    int target_tokens = (int)(session->config.context_window * 0.5);
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
                    messages_trimmed, session->conversation.count);

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