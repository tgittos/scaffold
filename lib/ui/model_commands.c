#include "model_commands.h"
#include "../agent/session.h"
#include "../util/config.h"
#include "../util/ansi_codes.h"
#include "../llm/model_capabilities.h"
#include "../agent/session_configurator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *find_tier_for_model(const char *model_id) {
    if (!model_id) return NULL;

    const char *simple = config_get_string("model_simple");
    const char *standard = config_get_string("model_standard");
    const char *high = config_get_string("model_high");

    if (simple && strcmp(model_id, simple) == 0) return "simple";
    if (standard && strcmp(model_id, standard) == 0) return "standard";
    if (high && strcmp(model_id, high) == 0) return "high";
    return NULL;
}

static void show_current_model(AgentSession *session) {
    const char *model = session->session_data.config.model;
    const char *tier = find_tier_for_model(model);

    if (tier) {
        printf(TERM_BOLD "Current model:" TERM_RESET " %s (tier: %s)\n", model, tier);
    } else {
        printf(TERM_BOLD "Current model:" TERM_RESET " %s\n", model ? model : "unknown");
    }
}

static void show_model_list(AgentSession *session) {
    const char *simple = config_get_string("model_simple");
    const char *standard = config_get_string("model_standard");
    const char *high = config_get_string("model_high");
    const char *current = session->session_data.config.model;

    printf(TERM_BOLD "Model tiers:" TERM_RESET "\n");
    printf("  simple   : %s%s\n", simple ? simple : "(not set)",
           (current && simple && strcmp(current, simple) == 0) ? " (active)" : "");
    printf("  standard : %s%s\n", standard ? standard : "(not set)",
           (current && standard && strcmp(current, standard) == 0) ? " (active)" : "");
    printf("  high     : %s%s\n", high ? high : "(not set)",
           (current && high && strcmp(current, high) == 0) ? " (active)" : "");
}

static void switch_model(const char *name, AgentSession *session) {
    const char *resolved = config_resolve_model(name);

    const char *current_url = session->session_data.config.api_url;
    APIType current_type = session_configurator_detect_api_type(current_url);

    int is_claude = (strstr(resolved, "claude") != NULL);
    int is_openai_url = (current_type == API_TYPE_OPENAI);
    int is_anthropic_url = (current_type == API_TYPE_ANTHROPIC);

    if (is_claude && is_openai_url) {
        printf("Cannot switch to '%s': current API URL points to OpenAI, not Anthropic.\n"
               "Update api_url in your config file to use Anthropic models.\n", resolved);
        return;
    }
    if (!is_claude && is_anthropic_url) {
        printf("Cannot switch to '%s': current API URL points to Anthropic.\n"
               "Only Claude models are compatible with the Anthropic API.\n", resolved);
        return;
    }

    char *dup = strdup(resolved);
    if (!dup) {
        printf("Error: out of memory switching model\n");
        return;
    }
    free(session->session_data.config.model);
    session->session_data.config.model = dup;

    config_set("model", resolved);

    ModelRegistry *registry = session->model_registry;
    if (registry && session->session_data.config.model) {
        ModelCapabilities *caps = detect_model_capabilities(registry, session->session_data.config.model);
        if (caps && caps->max_context_length > 0) {
            session->session_data.config.context_window = caps->max_context_length;
        }
    }

    const char *tier = find_tier_for_model(resolved);
    if (tier) {
        printf("Switched to " TERM_BOLD "%s" TERM_RESET " (tier: %s)\n", resolved, tier);
    } else {
        printf("Switched to " TERM_BOLD "%s" TERM_RESET "\n", resolved);
    }
}

int process_model_command(const char *args, AgentSession *session) {
    if (!args || !session) return -1;

    if (*args == '\0') {
        show_current_model(session);
        return 0;
    }

    if (strcmp(args, "list") == 0) {
        show_model_list(session);
        return 0;
    }

    switch_model(args, session);
    return 0;
}
