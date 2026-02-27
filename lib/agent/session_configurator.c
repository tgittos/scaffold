#include "session_configurator.h"
#include "../util/config.h"
#include "../util/prompt_loader.h"
#include "../util/debug_output.h"
#include "../util/app_home.h"
#include "../tools/tool_extension.h"
#include "../llm/model_capabilities.h"
#include "../llm/llm_provider.h"
#include "../llm/providers/codex_provider.h"
#include "../llm/embeddings_service.h"
#include "../auth/openai_login.h"
#include "../llm/llm_client.h"
#include <mbedtls/platform_util.h>
#include "../db/oauth2_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODEX_DEFAULT_MODEL "gpt-5.3-codex"

/* Persistent db_path for the credential provider callback */
static char *s_codex_db_path = NULL;

static int is_codex_url(const char *url) {
    return url && strstr(url, CODEX_URL_PATTERN) != NULL;
}

APIType session_configurator_detect_api_type(const char* api_url) {
    if (api_url == NULL) return API_TYPE_LOCAL;

    if (is_codex_url(api_url))
        return API_TYPE_OPENAI;
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

    if (session->model_override) {
        const char *resolved = config_resolve_model(session->model_override);
        if (resolved) {
            char *dup = strdup(resolved);
            if (dup) {
                free(session->session_data.config.model);
                session->session_data.config.model = dup;
                config_set("model", resolved);
            }
        }
    }

    if (config->api_key) {
        session->session_data.config.api_key = strdup(config->api_key);
        if (session->session_data.config.api_key == NULL) return -1;
    }

    /* Codex URL: inject OAuth credentials and set account ID header */
    if (is_codex_url(session->session_data.config.api_url)) {
        char *db = app_home_path("oauth2.db");
        if (db) {
            if (!openai_is_logged_in(db)) {
                fprintf(stderr, "Error: Codex URL requires OpenAI authentication.\n");
                fprintf(stderr, "   Run: scaffold --login\n");
                free(db);
                return -1;
            }
            char oauth_token[OAUTH2_MAX_TOKEN_LEN] = {0};
            char account_id[OAUTH2_MAX_ACCOUNT_ID_LEN] = {0};
            if (openai_get_codex_credentials(db, oauth_token, sizeof(oauth_token),
                                              account_id, sizeof(account_id)) == 0) {
                free(session->session_data.config.api_key);
                session->session_data.config.api_key = strdup(oauth_token);
                codex_set_account_id(account_id);
                free(s_codex_db_path);
                s_codex_db_path = strdup(db);
                llm_client_set_credential_provider(openai_refresh_credential, s_codex_db_path);
                debug_printf("Using OAuth credentials for Codex API (account: %s)\n", account_id);
                mbedtls_platform_zeroize(oauth_token, sizeof(oauth_token));
            } else {
                fprintf(stderr, "Error: OAuth tokens found but credential retrieval failed.\n");
                fprintf(stderr, "   Try: scaffold --logout && scaffold --login\n");
                free(db);
                return -1;
            }
            free(db);
        }
    }

    session->session_data.config.context_window = config->context_window;
    session->session_data.config.max_tokens = config->max_tokens;
    session->session_data.config.enable_streaming = config->enable_streaming;

    /* Codex Responses API only works with streaming; the buffered code path
       sends OpenAI Chat Completions format which Codex rejects. */
    if (is_codex_url(session->session_data.config.api_url)) {
        if (!session->session_data.config.enable_streaming) {
            debug_printf("Forcing streaming=true for Codex API (non-streaming not supported)\n");
            session->session_data.config.enable_streaming = 1;
        }
        /* Codex subscription API only accepts the default Codex model */
        free(session->session_data.config.model);
        session->session_data.config.model = strdup(CODEX_DEFAULT_MODEL);
        debug_printf("Forcing model to %s for Codex API\n", CODEX_DEFAULT_MODEL);
    }

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

void session_configurator_cleanup(void) {
    free(s_codex_db_path);
    s_codex_db_path = NULL;
}
