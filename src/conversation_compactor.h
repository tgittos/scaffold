#ifndef CONVERSATION_COMPACTOR_H
#define CONVERSATION_COMPACTOR_H

#include "conversation_tracker.h"
#include "session_manager.h"

// Configuration for conversation compaction
typedef struct {
    int preserve_recent_messages;     // Always keep this many recent messages
    int preserve_recent_tools;        // Always keep this many recent tool interactions
    int min_segment_size;            // Minimum messages to compact at once
    int max_segment_size;            // Maximum messages to compact at once
    float compaction_ratio;          // Target reduction ratio (0.0-1.0)
} CompactionConfig;

// Result of a compaction operation
typedef struct {
    int messages_compacted;          // Number of original messages compacted
    int messages_after_compaction;   // Number of messages after compaction
    int tokens_saved;               // Estimated tokens saved
    char* summary_content;          // The generated summary (caller must free)
} CompactionResult;

// Initialize compaction configuration with sensible defaults
void compaction_config_init(CompactionConfig* config);

// Analyze conversation to determine if compaction is needed
int should_compact_conversation(const ConversationHistory* conversation, 
                               const CompactionConfig* config,
                               int current_token_count, 
                               int target_token_count);

// Find the best segment of conversation to compact
int find_compaction_segment(const ConversationHistory* conversation,
                           const CompactionConfig* config,
                           int* start_index, int* end_index);

// Generate AI summary of conversation segment
int generate_conversation_summary(const SessionData* session,
                                 const ConversationHistory* conversation,
                                 int start_index, int end_index,
                                 char** summary_content);

// Perform conversation compaction by replacing segment with summary
int compact_conversation_segment(ConversationHistory* conversation,
                                const CompactionConfig* config,
                                int start_index, int end_index,
                                const char* summary_content,
                                CompactionResult* result);

// High-level function to perform full conversation compaction
int compact_conversation(SessionData* session, 
                        const CompactionConfig* config,
                        int target_token_count,
                        CompactionResult* result);

// Save compacted conversation back to CONVERSATION.md
int save_compacted_conversation(const ConversationHistory* conversation);

// Free memory allocated in CompactionResult
void cleanup_compaction_result(CompactionResult* result);

#endif // CONVERSATION_COMPACTOR_H