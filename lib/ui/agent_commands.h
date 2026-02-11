#ifndef AGENT_COMMANDS_H
#define AGENT_COMMANDS_H

typedef struct AgentSession AgentSession;

int process_agent_command(const char *args, AgentSession *session);

#endif /* AGENT_COMMANDS_H */
