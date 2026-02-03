#ifndef MEMORY_COMMANDS_H
#define MEMORY_COMMANDS_H

// Returns 0 if command was handled, -1 if not a memory command
int process_memory_command(const char* command);

void memory_commands_init(void);
void memory_commands_cleanup(void);

#endif
