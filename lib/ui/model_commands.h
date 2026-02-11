#ifndef MODEL_COMMANDS_H
#define MODEL_COMMANDS_H

typedef struct AgentSession AgentSession;

/**
 * Process a /model command.
 *
 * Handles:
 *   /model           -- show current model
 *   /model list      -- show all tiers
 *   /model <name>    -- switch model (tier name or raw model ID)
 *
 * @param args  Everything after "/model " (may be empty string)
 * @param session  The current agent session
 * @return 0 if command was handled, -1 on error
 */
int process_model_command(const char *args, AgentSession *session);

#endif /* MODEL_COMMANDS_H */
