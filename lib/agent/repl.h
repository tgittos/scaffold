/* Callback-based REPL for interactive agent mode. */
#ifndef LIB_AGENT_REPL_H
#define LIB_AGENT_REPL_H

#include <time.h>
#include "session.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run the interactive REPL loop for a session.
 *
 * This function blocks until the user exits (quit, exit, or Ctrl+D).
 * It handles:
 * - Readline input with history
 * - Async message processing
 * - Message polling notifications
 *
 * @param session The session to run interactively
 * @param json_mode Whether JSON output mode is enabled
 * @return 0 on success, non-zero on error
 */
int repl_run_session(AgentSession* session, bool json_mode);

/**
 * Display welcome message or recap for interactive session.
 *
 * @param session The session
 * @param json_mode Whether JSON output mode is enabled
 * @param last_time Timestamp of most recent message (0 = query the document store)
 * @param now Current time (0 = use wall clock)
 */
void repl_show_greeting(AgentSession* session, bool json_mode, time_t last_time, time_t now);

#ifdef __cplusplus
}
#endif

#endif /* LIB_AGENT_REPL_H */
