#ifndef MODE_COMMANDS_H
#define MODE_COMMANDS_H

typedef struct AgentSession AgentSession;

/**
 * Process a /mode command.
 *
 * Handles:
 *   /mode           -- show current mode
 *   /mode list      -- show all modes with descriptions
 *   /mode <name>    -- switch mode
 *
 * @param args  Everything after "/mode " (may be empty string)
 * @param session  The current agent session
 * @return 0 if command was handled, -1 on error
 */
int process_mode_command(const char *args, AgentSession *session);

#endif /* MODE_COMMANDS_H */
