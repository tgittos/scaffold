#include "message_dispatcher.h"
#include "context_enhancement.h"
#include "../network/api_common.h"
#include "../plugin/hook_dispatcher.h"
#include "../util/debug_output.h"
#include <stdlib.h>

static const DispatchDecision BUFFERED_DECISION = { .mode = DISPATCH_BUFFERED, .provider = NULL };

DispatchDecision message_dispatcher_select_mode(const AgentSession* session) {
    if (session == NULL) return BUFFERED_DECISION;

    if (!session->session_data.config.enable_streaming) {
        debug_printf("Using buffered mode (streaming disabled via configuration)\n");
        return BUFFERED_DECISION;
    }

    ProviderRegistry* provider_registry = get_provider_registry();
    LLMProvider* provider = NULL;
    if (provider_registry != NULL) {
        provider = detect_provider_for_url(provider_registry, session->session_data.config.api_url);
    }

    bool provider_supports_streaming = provider != NULL &&
                                       provider->supports_streaming != NULL &&
                                       provider->supports_streaming(provider) &&
                                       provider->build_streaming_request_json != NULL &&
                                       provider->parse_stream_event != NULL;

    if (provider_supports_streaming) {
        debug_printf("Using streaming mode for provider: %s\n", provider->capabilities.name);
        return (DispatchDecision){ .mode = DISPATCH_STREAMING, .provider = provider };
    }

    debug_printf("Using buffered mode (provider does not support streaming)\n");
    return BUFFERED_DECISION;
}

char* message_dispatcher_build_payload(AgentSession* session,
                                       const char* user_message,
                                       int max_tokens) {
    if (session == NULL) return NULL;

    EnhancedPromptParts parts;
    if (build_enhanced_prompt_parts(session, user_message, &parts) != 0) return NULL;

    hook_dispatch_pre_llm_send(&session->plugin_manager, session,
                                &parts.base_prompt, &parts.dynamic_context);

    SystemPromptParts sys_parts = {
        .base_prompt = parts.base_prompt,
        .dynamic_context = parts.dynamic_context
    };

    int is_anthropic = (session->session_data.config.api_type == API_TYPE_ANTHROPIC);

    char* result = build_json_payload_common(
        session->session_data.config.model, &sys_parts,
        &session->session_data.conversation, user_message,
        is_anthropic ? "max_tokens" : session->session_data.config.max_tokens_param,
        max_tokens, &session->tools,
        is_anthropic ? format_anthropic_message : format_openai_message,
        is_anthropic ? 1 : 0);

    free_enhanced_prompt_parts(&parts);
    return result;
}
