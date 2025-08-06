#include "token_manager.h"
#include "debug_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Include ralph.h after token_manager.h to resolve the struct RalphSession
#include "ralph.h"

// Default token configuration values
#define DEFAULT_MIN_RESPONSE_TOKENS 150
#define DEFAULT_SAFETY_BUFFER_BASE 50
#define DEFAULT_SAFETY_BUFFER_RATIO 0.02f  // 2% of context window - reduced due to better estimation
#define DEFAULT_CHARS_PER_TOKEN 5.5f       // Modern tokenizers: ~1 token per word (4-6 chars)

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
    
    // Adjust estimation based on content type
    // Code and JSON are more efficiently tokenized
    if (strstr(text, "```") != NULL || strstr(text, "function ") != NULL || 
        strstr(text, "#include") != NULL || strstr(text, "def ") != NULL) {
        chars_per_token *= 1.2f; // Code is ~20% more efficient
    }
    
    // JSON content is very efficiently tokenized
    if (text[0] == '{' || strstr(text, "\"role\":") != NULL) {
        chars_per_token *= 1.3f; // JSON is ~30% more efficient
    }
    
    int estimated_tokens = (int)ceil(char_count / chars_per_token);
    
    // Add overhead for JSON structure, tool definitions, etc.
    if (strstr(text, "\"tools\"") != NULL) {
        estimated_tokens += 50; // Reduced overhead - was too conservative
    }
    if (strstr(text, "\"system\"") != NULL) {
        estimated_tokens += 10; // Reduced overhead
    }
    
    return estimated_tokens;
}

int get_dynamic_safety_buffer(const TokenConfig* config, int estimated_prompt_tokens) {
    if (config == NULL) return DEFAULT_SAFETY_BUFFER_BASE;
    
    // Base safety buffer
    int buffer = config->safety_buffer_base;
    
    // Add percentage-based buffer
    int ratio_buffer = (int)(config->context_window * config->safety_buffer_ratio);
    buffer += ratio_buffer;
    
    // Increase buffer for complex prompts (more tool calls, longer conversations)
    if (estimated_prompt_tokens > config->context_window * 0.7) {
        buffer += 50; // Extra buffer for complex contexts
    }
    
    // Ensure minimum safety buffer
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
    
    // Calculate current token usage
    for (int i = 0; i < conversation->count; i++) {
        current_tokens += estimate_token_count(conversation->messages[i].content, config);
        current_tokens += 10; // Overhead per message for role, structure, etc.
    }
    
    debug_printf("Current conversation tokens: %d, max allowed: %d\n", current_tokens, max_prompt_tokens);
    
    // Trim from oldest messages (but preserve recent tool interactions)
    while (current_tokens > max_prompt_tokens && conversation->count > 0) {
        // Find the oldest non-tool message to remove
        int remove_index = -1;
        
        // Look for old user/assistant pairs, but preserve recent tool interactions
        for (int i = 0; i < conversation->count - 2; i++) { // Keep last 2 messages
            if (strcmp(conversation->messages[i].role, "tool") != 0) {
                remove_index = i;
                break;
            }
        }
        
        // If no non-tool message found, remove the oldest message
        if (remove_index == -1 && conversation->count > 1) {
            remove_index = 0;
        }
        
        if (remove_index == -1) break; // Can't trim anymore
        
        // Remove the message
        int removed_tokens = estimate_token_count(conversation->messages[remove_index].content, config) + 10;
        current_tokens -= removed_tokens;
        
        // Free memory
        free(conversation->messages[remove_index].role);
        free(conversation->messages[remove_index].content);
        free(conversation->messages[remove_index].tool_call_id);
        free(conversation->messages[remove_index].tool_name);
        
        // Shift remaining messages
        for (int j = remove_index; j < conversation->count - 1; j++) {
            conversation->messages[j] = conversation->messages[j + 1];
        }
        
        conversation->count--;
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
    
    // Initialize usage struct
    memset(usage, 0, sizeof(TokenUsage));
    
    // Validate configuration
    if (validate_token_config(config) != 0) {
        return -1;
    }
    
    // Use the configured context window
    int effective_context_window = config->context_window;
    
    usage->context_window_used = effective_context_window;
    
    // Estimate tokens for system prompt
    int system_tokens = 0;
    if (session->config.system_prompt) {
        system_tokens = estimate_token_count(session->config.system_prompt, config);
    }
    
    // Estimate tokens for user message
    int user_tokens = user_message ? estimate_token_count(user_message, config) : 0;
    
    // Estimate tokens for conversation history
    int history_tokens = 0;
    for (int i = 0; i < session->conversation.count; i++) {
        history_tokens += estimate_token_count(session->conversation.messages[i].content, config);
        history_tokens += 10; // Overhead per message
    }
    
    // Estimate tokens for tool definitions
    int tool_tokens = 0;
    if (session->tool_count > 0) {
        tool_tokens = session->tool_count * 50; // Rough estimate per tool
    }
    
    int total_prompt_tokens = system_tokens + user_tokens + history_tokens + tool_tokens + 50; // JSON overhead
    usage->total_prompt_tokens = total_prompt_tokens;
    
    // Calculate dynamic safety buffer
    int safety_buffer = get_dynamic_safety_buffer(config, total_prompt_tokens);
    usage->safety_buffer_used = safety_buffer;
    
    // Calculate available response tokens
    int available_tokens = effective_context_window - total_prompt_tokens - safety_buffer;
    
    // Apply model-specific output token limits based on actual model capabilities
    if (session->config.model) {
        // Get model-specific response limits
        int max_response_cap = -1;  // No cap by default
        
        // Determine response cap based on model pattern
        if (strstr(session->config.model, "claude") != NULL) {
            max_response_cap = 60000;  // Claude models have ~64k output limit
        } else if (strstr(session->config.model, "gpt") != NULL) {
            max_response_cap = 4000;   // OpenAI GPT models have ~4k output limit 
        } else if (strstr(session->config.model, "deepseek") != NULL) {
            max_response_cap = 8000;   // DeepSeek models have ~8k output limit
        } else if (strstr(session->config.model, "qwen") != NULL) {
            max_response_cap = 8000;   // Qwen models have ~8k output limit
        }
        // Default/local models: no specific cap
        
        if (max_response_cap > 0 && available_tokens > max_response_cap) {
            available_tokens = max_response_cap;
            debug_printf("Applied model-specific response token cap: %d tokens for model %s\n", 
                        max_response_cap, session->config.model);
        }
    }
    
    // Note: If available_tokens < min_response_tokens, the caller should use
    // manage_conversation_tokens() which will attempt compaction
    
    usage->available_response_tokens = available_tokens;
    
    debug_printf("Token allocation - Prompt: %d, Response: %d, Safety: %d, Context: %d\n",
                usage->total_prompt_tokens, usage->available_response_tokens, 
                usage->safety_buffer_used, usage->context_window_used);
    
    return 0;
}

// manage_conversation_tokens moved to ralph.c to avoid circular dependencies