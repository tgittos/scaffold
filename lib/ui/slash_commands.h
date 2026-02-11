#ifndef SLASH_COMMANDS_H
#define SLASH_COMMANDS_H

typedef struct AgentSession AgentSession;

typedef int (*SlashCommandHandler)(const char *args, AgentSession *session);

int slash_command_register(const char *name, const char *description,
                           SlashCommandHandler handler);
int slash_command_dispatch(const char *line, AgentSession *session);
void slash_commands_init(AgentSession *session);
void slash_commands_cleanup(void);

#endif /* SLASH_COMMANDS_H */
