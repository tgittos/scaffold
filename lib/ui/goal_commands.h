#ifndef GOAL_COMMANDS_H
#define GOAL_COMMANDS_H

typedef struct AgentSession AgentSession;

int process_goals_command(const char *args, AgentSession *session);

#endif /* GOAL_COMMANDS_H */
