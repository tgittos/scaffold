#ifndef CONVERSATION_COMPACTOR_H
#define CONVERSATION_COMPACTOR_H

#include "conversation_tracker.h"
#include "session_manager.h"

#define COMPACT_ERROR -1
#define COMPACT_SUCCESS_NO_WORK 0
#define COMPACT_SUCCESS_TRIMMED 1

typedef struct {
    int preserve_recent_messages;
    int background_threshold;  // Token count that triggers background trimming
} CompactionConfig;

typedef struct {
    int messages_trimmed;
    int messages_after_trimming;
    int tokens_saved;
} CompactionResult;

void compaction_config_init(CompactionConfig* config);
int should_background_compact(const ConversationHistory* conversation,
                             const CompactionConfig* config,
                             int current_token_count);
int background_compact_conversation(SessionData* session,
                                   const CompactionConfig* config,
                                   CompactionResult* result);
int compact_conversation(SessionData* session,
                        const CompactionConfig* config,
                        int target_token_count,
                        CompactionResult* result);
void cleanup_compaction_result(CompactionResult* result);

#endif // CONVERSATION_COMPACTOR_H