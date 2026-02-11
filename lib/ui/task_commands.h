#ifndef TASK_COMMANDS_H
#define TASK_COMMANDS_H

typedef struct AgentSession AgentSession;

int process_task_command(const char *args, AgentSession *session);

#endif /* TASK_COMMANDS_H */
