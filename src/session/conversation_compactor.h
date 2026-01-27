#ifndef CONVERSATION_COMPACTOR_H
#define CONVERSATION_COMPACTOR_H

#include "conversation_tracker.h"
#include "session_manager.h"

// Compaction return values
#define COMPACT_ERROR -1           // Error occurred during compaction
#define COMPACT_SUCCESS_NO_WORK 0  // Success, no trimming was needed
#define COMPACT_SUCCESS_TRIMMED 1  // Success, messages were trimmed

// Simplified configuration for conversation trimming
typedef struct {
    int preserve_recent_messages;     // Always keep this many recent messages
    int background_threshold;         // Token count to trigger background trimming
} CompactionConfig;

// Result of a trimming operation
typedef struct {
    int messages_trimmed;            // Number of messages removed
    int messages_after_trimming;     // Number of messages after trimming
    int tokens_saved;               // Estimated tokens saved
} CompactionResult;

// Initialize compaction configuration with sensible defaults
void compaction_config_init(CompactionConfig* config);

// Check if background trimming should be triggered
int should_background_compact(const ConversationHistory* conversation,
                             const CompactionConfig* config,
                             int current_token_count);

// Simple background trimming that removes oldest messages
int background_compact_conversation(SessionData* session, 
                                   const CompactionConfig* config,
                                   CompactionResult* result);

// Trim conversation to fit within token limits (uses existing token_manager function)
int compact_conversation(SessionData* session, 
                        const CompactionConfig* config,
                        int target_token_count,
                        CompactionResult* result);

// Free memory allocated in CompactionResult
void cleanup_compaction_result(CompactionResult* result);

#endif // CONVERSATION_COMPACTOR_H