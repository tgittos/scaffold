#ifndef TOKEN_MANAGER_H
#define TOKEN_MANAGER_H

#include "conversation_tracker.h"

#include "session_manager.h"

typedef struct {
    int context_window;
    int min_response_tokens;
    int safety_buffer_base;
    float safety_buffer_ratio;    // Additional buffer as fraction of context window
    float chars_per_token;        // Heuristic for char-to-token estimation
} TokenConfig;

typedef struct {
    int total_prompt_tokens;
    int available_response_tokens;
    int safety_buffer_used;
    int context_window_used;
} TokenUsage;

void token_config_init(TokenConfig* config, int context_window);
int estimate_token_count(const char* text, const TokenConfig* config);
int calculate_token_allocation(const SessionData* session, const char* user_message,
                              TokenConfig* config, TokenUsage* usage);
int trim_conversation_for_tokens(ConversationHistory* conversation,
                                const TokenConfig* config, int max_prompt_tokens,
                                const char* system_prompt);
int get_dynamic_safety_buffer(const TokenConfig* config, int estimated_prompt_tokens);
int validate_token_config(const TokenConfig* config);

#endif // TOKEN_MANAGER_H