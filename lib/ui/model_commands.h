#ifndef MODEL_COMMANDS_H
#define MODEL_COMMANDS_H

typedef struct AgentSession AgentSession;

/**
 * Process a /model command.
 *
 * Handles:
 *   /model           — show current model
 *   /model list      — show all tiers
 *   /model <name>    — switch model (tier name or raw model ID)
 *
 * @return 0 if command was handled, -1 if not a /model command
 */
int process_model_command(const char *command, AgentSession *session);

#endif /* MODEL_COMMANDS_H */
