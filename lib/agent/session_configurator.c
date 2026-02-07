/**
 * lib/agent/session_configurator.c - Session Configuration Loading
 *
 * Extracts configuration loading logic from session.c: API settings,
 * API type detection, embeddings reinitialization, system prompt loading,
 * and context window auto-configuration from model capabilities.
 */

#include "session_configurator.h"
#include "../util/config.h"
#include "../util/prompt_loader.h"
#include "../util/debug_output.h"
#include "../tools/tool_extension.h"
#include "../llm/model_capabilities.h"
#include "../llm/embeddings_service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

APIType session_configurator_detect_api_type(const char* api_url) {
    if (api_url == NULL) return API_TYPE_LOCAL;

    if (strstr(api_url, "api.openai.com") != NULL)
        return API_TYPE_OPENAI;
    if (strstr(api_url, "api.anthropic.com") != NULL)
        return API_TYPE_ANTHROPIC;
    return API_TYPE_LOCAL;
}

int session_configurator_load(AgentSession* session) {
    if (session == NULL) return -1;

    if (config_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize configuration system\n");
        return -1;
    }

    agent_config_t *config = config_get();
    if (!config) {
        fprintf(stderr, "Error: Failed to get configuration instance\n");
        return -1;
    }

    session->model_registry = get_model_registry();

    embeddings_service_t* embeddings = services_get_embeddings(session->services);
    if (embeddings) {
        embeddings_service_reinitialize(embeddings);
    }

    char *tools_desc = tool_extension_get_tools_description();
    load_system_prompt(&session->session_data.config.system_prompt, tools_desc);
    free(tools_desc);

    if (config->api_url) {
        session->session_data.config.api_url = strdup(config->api_url);
        if (session->session_data.config.api_url == NULL) return -1;
    }

    if (config->model) {
        session->session_data.config.model = strdup(config->model);
        if (session->session_data.config.model == NULL) return -1;
    }

    if (config->api_key) {
        session->session_data.config.api_key = strdup(config->api_key);
        if (session->session_data.config.api_key == NULL) return -1;
    }

    session->session_data.config.context_window = config->context_window;
    session->session_data.config.max_tokens = config->max_tokens;
    session->session_data.config.enable_streaming = config->enable_streaming;

    session->session_data.config.api_type = session_configurator_detect_api_type(
        session->session_data.config.api_url);

    switch (session->session_data.config.api_type) {
    case API_TYPE_OPENAI:
        session->session_data.config.max_tokens_param = "max_completion_tokens";
        break;
    case API_TYPE_ANTHROPIC:
    case API_TYPE_LOCAL:
        session->session_data.config.max_tokens_param = "max_tokens";
        break;
    }

    /* 8192 is the fallback context window; upgrade to model-specific size when available */
    if (session->session_data.config.context_window == 8192) {
        ModelRegistry* registry = session->model_registry;
        if (registry && session->session_data.config.model) {
            ModelCapabilities* model = detect_model_capabilities(registry, session->session_data.config.model);
            if (model && model->max_context_length > 0) {
                session->session_data.config.context_window = model->max_context_length;
                debug_printf("Auto-configured context window from model capabilities: %d tokens for model %s\n",
                            model->max_context_length, session->session_data.config.model);
            } else {
                debug_printf("Using default context window (%d tokens) - no model capabilities found for model %s\n",
                            session->session_data.config.context_window,
                            session->session_data.config.model ? session->session_data.config.model : "unknown");
            }
        }
    }

    return 0;
}
