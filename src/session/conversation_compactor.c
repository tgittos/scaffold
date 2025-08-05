#include "conversation_compactor.h"
#include "token_manager.h"
#include "debug_output.h"
#include "json_utils.h"
#include "../db/vector_db_service.h"
#include "../llm/embeddings_service.h"
#include "../utils/common_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Default compaction configuration
#define DEFAULT_PRESERVE_RECENT_MESSAGES 5
#define DEFAULT_PRESERVE_RECENT_TOOLS 3
#define DEFAULT_MIN_SEGMENT_SIZE 4
#define DEFAULT_MAX_SEGMENT_SIZE 20
#define DEFAULT_COMPACTION_RATIO 0.3f
#define DEFAULT_BACKGROUND_THRESHOLD 0.4f  // 40% of context window
#define DEFAULT_STORE_IN_VECTOR_DB 1       // Enable by default

// Vector database constants
#define CONVERSATION_INDEX_NAME "conversation_history"

// Summarization prompt that preserves critical details
static const char* SUMMARIZATION_PROMPT = 
"You are an expert at creating precise, information-dense summaries of technical conversations. "
"Your task is to summarize a segment of a conversation between a user and an AI assistant while preserving ALL critical information.\n\n"

"CRITICAL REQUIREMENTS:\n"
"- Preserve ALL technical details, file names, code snippets, commands, and error messages\n"
"- Maintain the logical flow of problem-solving and decision-making\n"
"- Keep all tool usage results, file contents, and system outputs\n"
"- Preserve context about what was tried, what worked, and what failed\n"
"- Maintain references to specific files, functions, line numbers, and code locations\n"
"- Keep all configuration details, environment variables, and system setup information\n"
"- Preserve debugging information, stack traces, and diagnostic output\n"
"- Maintain the relationship between user requests and assistant actions\n\n"

"FORMAT REQUIREMENTS:\n"
"- Use a structured format with clear sections\n"
"- Use bullet points and numbered lists for clarity\n"
"- Preserve exact command-line syntax and file paths\n"
"- Keep code blocks and technical syntax intact\n"
"- Use consistent terminology throughout\n\n"

"COMPRESSION STRATEGY:\n"
"- Consolidate repeated similar actions (e.g., \"Ran multiple grep searches to find X, Y, Z\")\n"
"- Merge related file operations (e.g., \"Examined files A, B, C to understand the system architecture\")\n"
"- Combine tool results when they build on each other\n"
"- Remove conversational pleasantries but keep all technical substance\n"
"- Condense explanations while keeping all factual content\n\n"

"EXAMPLE STRUCTURE:\n"
"## User Request\n[What the user asked for]\n\n"
"## Investigation\n[What files/systems were examined, key findings]\n\n"
"## Actions Taken\n[Specific commands, code changes, tool usage]\n\n"
"## Key Results\n[Important outputs, discoveries, outcomes]\n\n"
"## Context for Next Steps\n[State after this segment, what's ready for follow-up]\n\n"

"Now summarize this conversation segment, following all requirements above:\n\n";

void compaction_config_init(CompactionConfig* config) {
    if (config == NULL) return;
    
    config->preserve_recent_messages = DEFAULT_PRESERVE_RECENT_MESSAGES;
    config->preserve_recent_tools = DEFAULT_PRESERVE_RECENT_TOOLS;
    config->min_segment_size = DEFAULT_MIN_SEGMENT_SIZE;
    config->max_segment_size = DEFAULT_MAX_SEGMENT_SIZE;
    config->compaction_ratio = DEFAULT_COMPACTION_RATIO;
    config->background_threshold = (int)(8192 * DEFAULT_BACKGROUND_THRESHOLD); // Default to 40% of 8K context
    config->store_in_vector_db = DEFAULT_STORE_IN_VECTOR_DB;
}

int should_compact_conversation(const ConversationHistory* conversation, 
                               const CompactionConfig* config,
                               int current_token_count, 
                               int target_token_count) {
    if (conversation == NULL || config == NULL) {
        return 0;
    }
    
    // Don't compact if we don't have enough messages to preserve
    int min_messages_needed = config->preserve_recent_messages + config->min_segment_size;
    if (conversation->count <= min_messages_needed) {
        debug_printf("Not enough messages to compact: %d <= %d\n", 
                    conversation->count, min_messages_needed);
        return 0;
    }
    
    // Check if we need to reduce token count
    if (current_token_count <= target_token_count) {
        debug_printf("Token count within target: %d <= %d\n", 
                    current_token_count, target_token_count);
        return 0;
    }
    
    debug_printf("Compaction needed: %d messages, %d tokens (target: %d)\n",
                conversation->count, current_token_count, target_token_count);
    return 1;
}

int find_compaction_segment(const ConversationHistory* conversation,
                           const CompactionConfig* config,
                           int* start_index, int* end_index) {
    if (conversation == NULL || config == NULL || start_index == NULL || end_index == NULL) {
        return -1;
    }
    
    // Calculate boundaries
    int preserve_from_end = config->preserve_recent_messages;
    int max_end = conversation->count - preserve_from_end;
    
    if (max_end <= config->min_segment_size) {
        debug_printf("Cannot find valid compaction segment\n");
        return -1;
    }
    
    // Find the best segment to compact
    // Strategy: Look for older segments that don't contain recent tool interactions
    int tool_preserve_boundary = conversation->count;
    
    // Find the boundary for preserving recent tool interactions
    int recent_tools_found = 0;
    for (int i = conversation->count - 1; i >= 0 && recent_tools_found < config->preserve_recent_tools; i--) {
        if (strcmp(conversation->messages[i].role, "tool") == 0) {
            recent_tools_found++;
            if (recent_tools_found == config->preserve_recent_tools) {
                tool_preserve_boundary = i;
                break;
            }
        }
    }
    
    // Use the more restrictive boundary
    int effective_max_end = (tool_preserve_boundary < max_end) ? tool_preserve_boundary : max_end;
    
    if (effective_max_end <= config->min_segment_size) {
        debug_printf("Cannot find segment avoiding recent tools\n");
        return -1;
    }
    
    // Select segment to compact
    *start_index = 0;  // Start from beginning for simplicity
    *end_index = effective_max_end - 1;
    
    // Limit segment size
    if ((*end_index - *start_index + 1) > config->max_segment_size) {
        *end_index = *start_index + config->max_segment_size - 1;
    }
    
    // Ensure minimum segment size
    if ((*end_index - *start_index + 1) < config->min_segment_size) {
        *end_index = *start_index + config->min_segment_size - 1;
    }
    
    debug_printf("Selected compaction segment: messages %d to %d (%d messages)\n",
                *start_index, *end_index, *end_index - *start_index + 1);
    
    return 0;
}

int generate_conversation_summary(const SessionData* session,
                                 const ConversationHistory* conversation,
                                 int start_index, int end_index,
                                 char** summary_content) {
    if (session == NULL || conversation == NULL || summary_content == NULL) {
        return -1;
    }
    
    if (start_index < 0 || end_index >= conversation->count || start_index > end_index) {
        return -1;
    }
    
    // Build the conversation segment for summarization
    // Calculate total size needed
    size_t total_size = 0;
    for (int i = start_index; i <= end_index; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        total_size += strlen(msg->content) + 256; // Content + header overhead
    }
    
    char* segment_text = malloc(total_size + 1);
    if (segment_text == NULL) {
        return -1;
    }
    segment_text[0] = '\0';
    
    // Add each message in the segment
    for (int i = start_index; i <= end_index; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        
        // Format message with clear boundaries
        char message_header[256];
        if (strcmp(msg->role, "tool") == 0) {
            snprintf(message_header, sizeof(message_header), 
                    "\n--- TOOL RESPONSE (%s) ---\n", 
                    msg->tool_name ? msg->tool_name : "unknown");
        } else {
            snprintf(message_header, sizeof(message_header), 
                    "\n--- %s MESSAGE ---\n", 
                    strcmp(msg->role, "user") == 0 ? "USER" : "ASSISTANT");
        }
        
        strcat(segment_text, message_header);
        strcat(segment_text, msg->content);
        strcat(segment_text, "\n");
    }
    
    if (segment_text == NULL) {
        return -1;
    }
    
    debug_printf("Generating summary for %d messages using %zu character segment\n",
                end_index - start_index + 1, strlen(segment_text));
    
    // User-visible logging
    printf("📝 Compacting conversation history...\n");
    printf("   Summarizing %d messages (messages %d-%d) to reduce token usage\n", 
           end_index - start_index + 1, start_index, end_index);
    
    // Create summarization request
    ConversationHistory summary_conversation = {0};
    init_conversation_history(&summary_conversation);
    
    // Build the full summarization prompt
    size_t prompt_size = strlen(SUMMARIZATION_PROMPT) + strlen(segment_text) + 100;
    char* full_prompt = malloc(prompt_size);
    if (full_prompt == NULL) {
        free(segment_text);
        return -1;
    }
    
    strcpy(full_prompt, SUMMARIZATION_PROMPT);
    strcat(full_prompt, segment_text);
    strcat(full_prompt, "\n--- END OF SEGMENT TO SUMMARIZE ---\n");
    free(segment_text);
    
    // Note: Using clean conversation for summarization context
    
    // Use a smaller token limit for summary to ensure it completes
    int summary_max_tokens = 2000;  // Large enough for detailed summary, small enough to complete
    
    // Create temporary session for summarization
    SessionData temp_session;
    session_data_init(&temp_session);
    session_data_copy_config(&temp_session, &session->config);
    
    // Build JSON payload for summarization request
    char* post_data = session_build_api_payload(&temp_session, full_prompt, summary_max_tokens, 0);
    
    free(full_prompt);
    cleanup_conversation_history(&summary_conversation);
    
    if (post_data == NULL) {
        debug_printf("Error: Failed to build JSON payload for summarization\n");
        return -1;
    }
    
    // Make API request using session manager
    printf("   Calling AI to generate summary...\n");
    char* extracted_summary = NULL;
    
    if (session_make_api_request(&temp_session, post_data, &extracted_summary) != 0) {
        debug_printf("Error: API request failed for summarization\n");
        free(post_data);
        session_data_cleanup(&temp_session);
        return -1;
    }
    
    free(post_data);
    session_data_cleanup(&temp_session);
    
    if (extracted_summary == NULL) {
        debug_printf("Error: Failed to extract summary from API response\n");
        return -1;
    }
    
    debug_printf("Successfully generated summary: %zu characters\n", strlen(extracted_summary));
    printf("   ✅ Summary generated successfully (%zu characters)\n", strlen(extracted_summary));
    *summary_content = extracted_summary;
    
    return 0;
}

int compact_conversation_segment(ConversationHistory* conversation,
                                const CompactionConfig* config,
                                int start_index, int end_index,
                                const char* summary_content,
                                CompactionResult* result) {
    if (conversation == NULL || config == NULL || summary_content == NULL || result == NULL) {
        return -1;
    }
    
    if (start_index < 0 || end_index >= conversation->count || start_index > end_index) {
        return -1;
    }
    
    // Initialize result
    memset(result, 0, sizeof(CompactionResult));
    result->messages_compacted = end_index - start_index + 1;
    result->summary_content = strdup(summary_content);
    
    if (result->summary_content == NULL) {
        return -1;
    }
    
    // Calculate tokens saved (rough estimate)
    TokenConfig token_config;
    token_config_init(&token_config, 8192, 8192);
    
    int original_tokens = 0;
    for (int i = start_index; i <= end_index; i++) {
        original_tokens += estimate_token_count(conversation->messages[i].content, &token_config);
    }
    
    int summary_tokens = estimate_token_count(summary_content, &token_config);
    result->tokens_saved = original_tokens - summary_tokens;
    
    // Free the original messages that will be replaced
    for (int i = start_index; i <= end_index; i++) {
        free(conversation->messages[i].role);
        free(conversation->messages[i].content);
        free(conversation->messages[i].tool_call_id);
        free(conversation->messages[i].tool_name);
    }
    
    // Create the summary message
    char* summary_role = strdup("assistant");
    char* summary_copy = strdup(summary_content);
    
    if (summary_role == NULL || summary_copy == NULL) {
        free(summary_role);
        free(summary_copy);
        return -1;
    }
    
    // Replace the first message with the summary
    conversation->messages[start_index].role = summary_role;
    conversation->messages[start_index].content = summary_copy;
    conversation->messages[start_index].tool_call_id = NULL;
    conversation->messages[start_index].tool_name = NULL;
    
    // Shift remaining messages down
    int messages_to_shift = conversation->count - end_index - 1;
    if (messages_to_shift > 0) {
        memmove(&conversation->messages[start_index + 1],
                &conversation->messages[end_index + 1],
                messages_to_shift * sizeof(ConversationMessage));
    }
    
    // Update conversation count
    int messages_removed = end_index - start_index;  // -1 because we kept one for summary
    conversation->count -= messages_removed;
    result->messages_after_compaction = conversation->count;
    
    debug_printf("Compacted %d messages into 1 summary, saved ~%d tokens\n",
                result->messages_compacted, result->tokens_saved);
    
    // User-visible completion message
    printf("   📦 Compaction complete: %d messages → 1 summary (saved ~%d tokens)\n",
           result->messages_compacted, result->tokens_saved);
    
    return 0;
}

int compact_conversation(SessionData* session, 
                        const CompactionConfig* config,
                        int target_token_count,
                        CompactionResult* result) {
    if (session == NULL || config == NULL || result == NULL) {
        return -1;
    }
    
    // Calculate current token usage
    TokenConfig token_config;
    token_config_init(&token_config, session->config.context_window, session->config.max_context_window);
    
    int current_tokens = 0;
    for (int i = 0; i < session->conversation.count; i++) {
        current_tokens += estimate_token_count(session->conversation.messages[i].content, &token_config);
    }
    
    // Check if compaction is needed
    if (!should_compact_conversation(&session->conversation, config, current_tokens, target_token_count)) {
        return 0;  // No compaction needed
    }
    
    // User notification
    printf("🔄 Context window approaching limit (%d/%d tokens)\n", current_tokens, target_token_count);
    printf("   Initiating conversation compaction to maintain performance...\n");
    
    // Find segment to compact
    int start_index, end_index;
    if (find_compaction_segment(&session->conversation, config, &start_index, &end_index) != 0) {
        return -1;
    }
    
    // Generate summary
    char* summary_content = NULL;
    if (generate_conversation_summary(session, &session->conversation, start_index, end_index, &summary_content) != 0) {
        return -1;
    }
    
    // Store in vector database before compacting (if enabled)
    if (config->store_in_vector_db) {
        if (store_conversation_segment_in_vector_db(&session->conversation, start_index, end_index, summary_content) != 0) {
            debug_printf("Warning: Failed to store conversation segment in vector DB\n");
            // Don't fail the operation for this
        }
    }
    
    // Perform compaction
    int compact_result = compact_conversation_segment(&session->conversation, config, 
                                                     start_index, end_index, 
                                                     summary_content, result);
    
    free(summary_content);
    
    if (compact_result != 0) {
        return -1;
    }
    
    // Save the compacted conversation
    if (save_compacted_conversation(&session->conversation) != 0) {
        debug_printf("Warning: Failed to save compacted conversation to file\n");
        // Don't fail the operation for this
    }
    
    return 0;
}

int save_compacted_conversation(const ConversationHistory* conversation) {
    if (conversation == NULL) {
        return -1;
    }
    
    // Rewrite the entire conversation file
    FILE* file = fopen("CONVERSATION.md", "w");
    if (file == NULL) {
        return -1;
    }
    
    for (int i = 0; i < conversation->count; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        
        // Build JSON message
        JsonBuilder builder = {0};
        if (json_builder_init(&builder) != 0) {
            fclose(file);
            return -1;
        }
        
        json_builder_start_object(&builder);
        json_builder_add_string(&builder, "role", msg->role);
        json_builder_add_separator(&builder);
        json_builder_add_string(&builder, "content", msg->content);
        
        if (msg->tool_call_id != NULL) {
            json_builder_add_separator(&builder);
            json_builder_add_string(&builder, "tool_call_id", msg->tool_call_id);
        }
        
        if (msg->tool_name != NULL) {
            json_builder_add_separator(&builder);
            json_builder_add_string(&builder, "tool_name", msg->tool_name);
        }
        
        json_builder_end_object(&builder);
        
        char* json_message = json_builder_finalize(&builder);
        json_builder_cleanup(&builder);
        
        if (json_message == NULL) {
            fclose(file);
            return -1;
        }
        
        fprintf(file, "%s\n", json_message);
        free(json_message);
    }
    
    fclose(file);
    debug_printf("Saved compacted conversation with %d messages\n", conversation->count);
    return 0;
}

static vector_db_error_t ensure_conversation_index(void) {
    size_t dimension = embeddings_service_get_dimension();
    if (dimension == 0) {
        return VECTOR_DB_ERROR_INVALID_PARAM;
    }
    
    index_config_t config = vector_db_service_get_memory_config(dimension);
    vector_db_error_t result = vector_db_service_ensure_index(CONVERSATION_INDEX_NAME, &config);
    
    // Free allocated metric string from config
    free(config.metric);
    return result;
}


int store_conversation_segment_in_vector_db(const ConversationHistory* conversation,
                                           int start_index, int end_index,
                                           const char* summary_content) {
    if (conversation == NULL || summary_content == NULL) {
        return -1;
    }
    
    if (start_index < 0 || end_index >= conversation->count || start_index > end_index) {
        return -1;
    }
    
    // Check if embeddings service is configured
    if (!embeddings_service_is_configured()) {
        debug_printf("Embeddings service not configured, skipping vector DB storage\n");
        return 0; // Not an error, just skip
    }
    
    // Ensure conversation index exists
    vector_db_error_t index_err = ensure_conversation_index();
    if (index_err != VECTOR_DB_OK) {
        debug_printf("Failed to ensure conversation index: %d\n", index_err);
        return -1;
    }
    
    // Build the full content to store: original conversation + summary
    size_t total_size = strlen(summary_content) + 1024;
    for (int i = start_index; i <= end_index; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        total_size += strlen(msg->content) + 256;
    }
    
    char* full_content = malloc(total_size);
    if (full_content == NULL) {
        return -1;
    }
    
    // Start with the summary
    strcpy(full_content, "SUMMARY: ");
    strcat(full_content, summary_content);
    strcat(full_content, "\n\nORIGINAL CONVERSATION:\n");
    
    // Add original messages for complete context
    for (int i = start_index; i <= end_index; i++) {
        const ConversationMessage* msg = &conversation->messages[i];
        
        char message_header[256];
        if (strcmp(msg->role, "tool") == 0) {
            snprintf(message_header, sizeof(message_header), 
                    "\n--- TOOL RESPONSE (%s) ---\n", 
                    msg->tool_name ? msg->tool_name : "unknown");
        } else {
            snprintf(message_header, sizeof(message_header), 
                    "\n--- %s MESSAGE ---\n", 
                    strcmp(msg->role, "user") == 0 ? "USER" : "ASSISTANT");
        }
        
        strcat(full_content, message_header);
        strcat(full_content, msg->content);
        strcat(full_content, "\n");
    }
    
    // Generate embedding for the content
    vector_t *embedding = embeddings_service_text_to_vector(full_content);
    if (embedding == NULL) {
        free(full_content);
        debug_printf("Failed to generate embedding for conversation segment\n");
        return -1;
    }
    
    // Store in vector database
    vector_db_t *vector_db = vector_db_service_get_database();
    if (vector_db == NULL) {
        free(full_content);
        embeddings_service_free_vector(embedding);
        return -1;
    }
    
    // Use timestamp as label (simplified approach without metadata for now)
    size_t label = (size_t)time(NULL);
    
    vector_db_error_t add_err = vector_db_add_vector(vector_db, CONVERSATION_INDEX_NAME, 
                                                     embedding, label);
    
    free(full_content);
    embeddings_service_free_vector(embedding);
    
    if (add_err != VECTOR_DB_OK) {
        debug_printf("Failed to store conversation segment in vector DB: %d\n", add_err);
        return -1;
    }
    
    debug_printf("Stored conversation segment (messages %d-%d) in vector database\n", 
                start_index, end_index);
    printf("   💾 Stored conversation history in vector database for future retrieval\n");
    
    return 0;
}

int should_background_compact(const ConversationHistory* conversation,
                             const CompactionConfig* config,
                             int current_token_count) {
    if (conversation == NULL || config == NULL) {
        return 0;
    }
    
    // Don't compact if we don't have enough messages to preserve
    int min_messages_needed = config->preserve_recent_messages + config->min_segment_size;
    if (conversation->count <= min_messages_needed) {
        return 0;
    }
    
    // Check if we've reached the background threshold
    if (current_token_count >= config->background_threshold) {
        debug_printf("Background compaction triggered: %d tokens >= %d threshold\n", 
                    current_token_count, config->background_threshold);
        return 1;
    }
    
    return 0;
}

int background_compact_conversation(SessionData* session, 
                                   const CompactionConfig* config,
                                   CompactionResult* result) {
    if (session == NULL || config == NULL || result == NULL) {
        return -1;
    }
    
    // Calculate current token usage
    TokenConfig token_config;
    token_config_init(&token_config, session->config.context_window, session->config.max_context_window);
    
    int current_tokens = 0;
    for (int i = 0; i < session->conversation.count; i++) {
        current_tokens += estimate_token_count(session->conversation.messages[i].content, &token_config);
    }
    
    // Check if background compaction is needed
    if (!should_background_compact(&session->conversation, config, current_tokens)) {
        return 0;  // No compaction needed
    }
    
    // User notification
    printf("🔄 Background compaction triggered (%d tokens >= %d threshold)\n", 
           current_tokens, config->background_threshold);
    printf("   Proactively compacting conversation to maintain fast response times...\n");
    
    // Find segment to compact
    int start_index, end_index;
    if (find_compaction_segment(&session->conversation, config, &start_index, &end_index) != 0) {
        return -1;
    }
    
    // Generate summary
    char* summary_content = NULL;
    if (generate_conversation_summary(session, &session->conversation, start_index, end_index, &summary_content) != 0) {
        return -1;
    }
    
    // Store in vector database before compacting (if enabled)
    if (config->store_in_vector_db) {
        if (store_conversation_segment_in_vector_db(&session->conversation, start_index, end_index, summary_content) != 0) {
            debug_printf("Warning: Failed to store conversation segment in vector DB\n");
            // Don't fail the operation for this
        }
    }
    
    // Perform compaction
    int compact_result = compact_conversation_segment(&session->conversation, config, 
                                                     start_index, end_index, 
                                                     summary_content, result);
    
    free(summary_content);
    
    if (compact_result != 0) {
        return -1;
    }
    
    // Save the compacted conversation
    if (save_compacted_conversation(&session->conversation) != 0) {
        debug_printf("Warning: Failed to save compacted conversation to file\n");
        // Don't fail the operation for this
    }
    
    return 0;
}

void cleanup_compaction_result(CompactionResult* result) {
    if (result == NULL) {
        return;
    }
    
    free(result->summary_content);
    result->summary_content = NULL;
    memset(result, 0, sizeof(CompactionResult));
}