#include "token_manager.h"
#include "debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Must follow token_manager.h so SessionData is defined before RalphSession references it
#include "ralph.h"

#define DEFAULT_MIN_RESPONSE_TOKENS 150
#define DEFAULT_SAFETY_BUFFER_BASE 50
#define DEFAULT_SAFETY_BUFFER_RATIO 0.02f
#define DEFAULT_CHARS_PER_TOKEN 5.5f  // Typical modern tokenizer: ~1 token per 4-6 chars

// Per-message overhead accounts for JSON framing (role, separators, etc.)
#define TOKEN_OVERHEAD_PER_MESSAGE 10
#define TOKEN_OVERHEAD_PER_TOOL 50
#define TOKEN_OVERHEAD_JSON_STRUCTURE 50

void token_config_init(TokenConfig* config, int context_window) {
    if (config == NULL) return;
    
    config->context_window = context_window > 0 ? context_window : 8192;
    config->min_response_tokens = DEFAULT_MIN_RESPONSE_TOKENS;
    config->safety_buffer_base = DEFAULT_SAFETY_BUFFER_BASE;
    config->safety_buffer_ratio = DEFAULT_SAFETY_BUFFER_RATIO;
    config->chars_per_token = DEFAULT_CHARS_PER_TOKEN;
}

int estimate_token_count(const char* text, const TokenConfig* config) {
    if (text == NULL || config == NULL) return 0;
    
    int char_count = strlen(text);
    float chars_per_token = config->chars_per_token;
    
    // Code and JSON use more repetitive/structural tokens, so tokenizers
    // compress them better -- adjust the chars-per-token ratio upward.
    if (strstr(text, "```") != NULL || strstr(text, "function ") != NULL ||
        strstr(text, "#include") != NULL || strstr(text, "def ") != NULL) {
        chars_per_token *= 1.2f;
    }
    if (text[0] == '{' || strstr(text, "\"role\":") != NULL) {
        chars_per_token *= 1.3f;
    }
    
    int estimated_tokens = (int)ceil(char_count / chars_per_token);
    
    if (strstr(text, "\"tools\"") != NULL) {
        estimated_tokens += 50;
    }
    if (strstr(text, "\"system\"") != NULL) {
        estimated_tokens += 10;
    }
    
    return estimated_tokens;
}

int get_dynamic_safety_buffer(const TokenConfig* config, int estimated_prompt_tokens) {
    if (config == NULL) return DEFAULT_SAFETY_BUFFER_BASE;
    
    int buffer = config->safety_buffer_base;
    int ratio_buffer = (int)(config->context_window * config->safety_buffer_ratio);
    buffer += ratio_buffer;

    // Near-full contexts are more likely to hit edge cases in token estimation
    if (estimated_prompt_tokens > config->context_window * 0.7) {
        buffer += 50;
    }

    if (buffer < config->safety_buffer_base) {
        buffer = config->safety_buffer_base;
    }
    
    return buffer;
}

int validate_token_config(const TokenConfig* config) {
    if (config == NULL) return -1;
    
    if (config->context_window <= 0) {
        debug_printf("Invalid context window configuration\n");
        return -1;
    }
    
    if (config->min_response_tokens <= 0 || config->min_response_tokens >= config->context_window) {
        debug_printf("Invalid min_response_tokens configuration\n");
        return -1;
    }
    
    if (config->chars_per_token <= 0) {
        debug_printf("Invalid chars_per_token configuration\n");
        return -1;
    }
    
    return 0;
}

int trim_conversation_for_tokens(ConversationHistory* conversation, 
                                const TokenConfig* config, int max_prompt_tokens,
                                const char* system_prompt) {
    if (conversation == NULL || config == NULL) return -1;
    
    int trimmed_count = 0;
    int system_tokens = system_prompt ? estimate_token_count(system_prompt, config) : 0;
    int current_tokens = system_tokens;
    
    for (size_t i = 0; i < conversation->count; i++) {
        current_tokens += estimate_token_count(conversation->data[i].content, config);
        current_tokens += TOKEN_OVERHEAD_PER_MESSAGE;
    }
    
    debug_printf("Current conversation tokens: %d, max allowed: %d\n", current_tokens, max_prompt_tokens);
    
    // Trim oldest messages first, but prefer removing non-tool messages to avoid
    // breaking tool call/response sequences that the LLM needs for context.
    while (current_tokens > max_prompt_tokens && conversation->count > 0) {
        int remove_index = -1;

        for (size_t i = 0; i < conversation->count - 2; i++) {
            if (conversation->data[i].role != NULL &&
                strcmp(conversation->data[i].role, "tool") != 0) {
                remove_index = i;
                break;
            }
        }

        if (remove_index == -1 && conversation->count > 1) {
            remove_index = 0;
        }

        if (remove_index == -1) break;

        int removed_tokens = estimate_token_count(conversation->data[remove_index].content, config) + TOKEN_OVERHEAD_PER_MESSAGE;
        current_tokens -= removed_tokens;

        free(conversation->data[remove_index].role);
        free(conversation->data[remove_index].content);
        free(conversation->data[remove_index].tool_call_id);
        free(conversation->data[remove_index].tool_name);

        ConversationHistory_remove(conversation, remove_index);

        trimmed_count++;

        debug_printf("Trimmed message %d, remaining tokens: %d\n", remove_index, current_tokens);
    }
    
    if (trimmed_count > 0) {
        debug_printf("Trimmed %d messages to fit token limit\n", trimmed_count);
    }
    
    return trimmed_count;
}

int calculate_token_allocation(const SessionData* session, const char* user_message,
                              TokenConfig* config, TokenUsage* usage) {
    if (session == NULL || config == NULL || usage == NULL) return -1;
    
    memset(usage, 0, sizeof(TokenUsage));

    if (validate_token_config(config) != 0) {
        return -1;
    }
    
    int effective_context_window = config->context_window;
    usage->context_window_used = effective_context_window;

    int system_tokens = 0;
    if (session->config.system_prompt) {
        system_tokens = estimate_token_count(session->config.system_prompt, config);
    }
    
    int user_tokens = user_message ? estimate_token_count(user_message, config) : 0;

    int history_tokens = 0;
    for (size_t i = 0; i < session->conversation.count; i++) {
        history_tokens += estimate_token_count(session->conversation.data[i].content, config);
        history_tokens += TOKEN_OVERHEAD_PER_MESSAGE;
    }
    
    int tool_tokens = 0;
    if (session->tool_count > 0) {
        tool_tokens = session->tool_count * TOKEN_OVERHEAD_PER_TOOL;
    }
    
    int total_prompt_tokens = system_tokens + user_tokens + history_tokens + tool_tokens + TOKEN_OVERHEAD_JSON_STRUCTURE;
    usage->total_prompt_tokens = total_prompt_tokens;
    
    int safety_buffer = get_dynamic_safety_buffer(config, total_prompt_tokens);
    usage->safety_buffer_used = safety_buffer;
    
    int available_tokens = effective_context_window - total_prompt_tokens - safety_buffer;

    // Clamp to known provider-specific max output token limits, which are
    // independent of the context window and not discoverable at runtime.
    if (session->config.model) {
        int max_response_cap = -1;

        if (strstr(session->config.model, "claude") != NULL) {
            max_response_cap = 60000;
        } else if (strstr(session->config.model, "gpt") != NULL) {
            max_response_cap = 4000;
        } else if (strstr(session->config.model, "deepseek") != NULL) {
            max_response_cap = 8000;
        } else if (strstr(session->config.model, "qwen") != NULL) {
            max_response_cap = 8000;
        }
        
        if (max_response_cap > 0 && available_tokens > max_response_cap) {
            available_tokens = max_response_cap;
            debug_printf("Applied model-specific response token cap: %d tokens for model %s\n", 
                        max_response_cap, session->config.model);
        }
    }
    
    usage->available_response_tokens = available_tokens;
    
    debug_printf("Token allocation - Prompt: %d, Response: %d, Safety: %d, Context: %d\n",
                usage->total_prompt_tokens, usage->available_response_tokens, 
                usage->safety_buffer_used, usage->context_window_used);
    
    return 0;
}

