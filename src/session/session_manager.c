#include "session_manager.h"
#include "rolling_summary.h"
#include <stdlib.h>
#include <string.h>

void session_data_init(SessionData* session) {
    if (session == NULL) return;

    memset(session, 0, sizeof(SessionData));
    init_conversation_history(&session->conversation);
    rolling_summary_init(&session->rolling_summary);
}

void session_data_cleanup(SessionData* session) {
    if (session == NULL) return;

    free(session->config.api_url);
    free(session->config.model);
    free(session->config.api_key);
    free(session->config.system_prompt);
    cleanup_conversation_history(&session->conversation);
    rolling_summary_cleanup(&session->rolling_summary);

    memset(session, 0, sizeof(SessionData));
}