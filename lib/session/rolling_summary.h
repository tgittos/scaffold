#ifndef ROLLING_SUMMARY_H
#define ROLLING_SUMMARY_H

#include "conversation_tracker.h"

#define ROLLING_SUMMARY_MAX_TOKENS 800
#define ROLLING_SUMMARY_TARGET_TOKENS 600

// Compaction thresholds as fractions of context window
#define COMPACTION_TRIGGER_THRESHOLD 0.75f   // Trigger when context reaches 75%
#define COMPACTION_TARGET_THRESHOLD 0.35f    // Trim down to 35% of context

typedef struct {
    char* summary_text;
    int estimated_tokens;
    int messages_summarized;
} RollingSummary;

void rolling_summary_init(RollingSummary* summary);
void rolling_summary_cleanup(RollingSummary* summary);

/**
 * Generate or update a rolling summary of messages about to be trimmed.
 *
 * @param api_url The LLM API URL to use for summarization
 * @param api_key The API key for authentication
 * @param api_type The API type (0=OpenAI, 1=Anthropic, 2=Local)
 * @param model The model to use for summarization
 * @param messages The messages to summarize (oldest messages being trimmed)
 * @param message_count Number of messages to summarize
 * @param existing_summary Previous rolling summary to merge with (may be NULL)
 * @param out_summary Output: newly allocated summary text (caller must free)
 * @return 0 on success, -1 on failure
 */
int generate_rolling_summary(
    const char* api_url,
    const char* api_key,
    int api_type,
    const char* model,
    const ConversationMessage* messages,
    int message_count,
    const char* existing_summary,
    char** out_summary
);

#endif // ROLLING_SUMMARY_H
