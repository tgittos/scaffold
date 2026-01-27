#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include "conversation_tracker.h"

#include "session_manager.h"

// Token calculation configuration
typedef struct {
    int context_window;           // Current model context window
    int min_response_tokens;      // Minimum tokens reserved for response
    int safety_buffer_base;       // Base safety buffer tokens
    float safety_buffer_ratio;    // Additional buffer as ratio of context
    float chars_per_token;        // Character to token conversion ratio
} TokenConfig;

// Token usage breakdown
typedef struct {
    int total_prompt_tokens;      // Total tokens in prompt
    int available_response_tokens; // Tokens available for response
    int safety_buffer_used;       // Actual safety buffer applied
    int context_window_used;      // Context window being used
} TokenUsage;

// Initialize token configuration with sensible defaults
void token_config_init(TokenConfig* config, int context_window);

// Estimate token count from text
int estimate_token_count(const char* text, const TokenConfig* config);

// Calculate optimal token allocation for a request
int calculate_token_allocation(const SessionData* session, const char* user_message,
                              TokenConfig* config, TokenUsage* usage);

// Trim conversation history to fit within token limits
int trim_conversation_for_tokens(ConversationHistory* conversation, 
                                const TokenConfig* config, int max_prompt_tokens,
                                const char* system_prompt);

// Get dynamic safety buffer based on context complexity
int get_dynamic_safety_buffer(const TokenConfig* config, int estimated_prompt_tokens);

// Validate token configuration  
int validate_token_config(const TokenConfig* config);

// Note: manage_conversation_tokens moved to ralph.c to avoid circular dependencies

#endif // TOKEN_MANAGER_H