/**
 * lib/ui/repl.h - REPL (Read-Eval-Print Loop) abstraction
 *
 * Provides a callback-based REPL that can be used by agents in interactive mode.
 */

#ifndef LIB_UI_REPL_H
#define LIB_UI_REPL_H

#include "../agent/session.h"

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
 * - Subagent approval proxying
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
 */
void repl_show_greeting(AgentSession* session, bool json_mode);

#ifdef __cplusplus
}
#endif

#endif /* LIB_UI_REPL_H */
