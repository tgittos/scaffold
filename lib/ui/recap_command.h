#ifndef RECAP_COMMAND_H
#define RECAP_COMMAND_H

typedef struct AgentSession AgentSession;

/**
 * Handle the /recap slash command.
 *
 * Generates a summary of recent conversation history via
 * session_generate_recap. Output goes to stdout (streaming).
 *
 * @param args  Arguments after "/recap" (currently unused)
 * @param session  The current agent session
 * @return 0 on success, -1 on failure
 */
int process_recap_command(const char *args, AgentSession *session);

#endif /* RECAP_COMMAND_H */
