#include "message_dispatcher.h"
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

    if (session->session_data.config.api_type == API_TYPE_ANTHROPIC) {
        return session_build_anthropic_json_payload(session, user_message, max_tokens);
    }
    return session_build_json_payload(session, user_message, max_tokens);
}
