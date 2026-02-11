#ifndef MEMORY_COMMANDS_H
#define MEMORY_COMMANDS_H

typedef struct Services Services;

/** Set the Services container for memory commands. */
void memory_commands_set_services(Services* services);

// Returns 0 if command was handled, -1 if not a memory command
int process_memory_command(const char* command);

#endif
