#ifndef LIB_AGENT_SESSION_CONFIGURATOR_H
#define LIB_AGENT_SESSION_CONFIGURATOR_H

#include "session.h"

/**
 * Load all configuration into a session.
 * Initializes the config system, copies API settings, detects API type,
 * loads the system prompt, and auto-configures context window from model
 * capabilities. Uses session->services for embeddings reinitialization.
 *
 * @param session The session to configure
 * @return 0 on success, -1 on failure
 */
int session_configurator_load(AgentSession* session);

APIType session_configurator_detect_api_type(const char* api_url);

/**
 * Free module-level state (e.g. cached db path for credential provider).
 * Safe to call multiple times.
 */
void session_configurator_cleanup(void);

#endif
