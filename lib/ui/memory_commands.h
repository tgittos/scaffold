#ifndef MEMORY_COMMANDS_H
#define MEMORY_COMMANDS_H

typedef struct AgentSession AgentSession;

// Returns 0 if command was handled, -1 if not a memory command
int process_memory_command(const char *args, AgentSession *session);

#endif
