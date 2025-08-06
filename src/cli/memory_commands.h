#ifndef MEMORY_COMMANDS_H
#define MEMORY_COMMANDS_H

// Process memory management commands
// Returns 0 if command was handled, -1 if not a memory command
int process_memory_command(const char* command);

// Initialize memory command system
void memory_commands_init(void);

// Cleanup memory command system
void memory_commands_cleanup(void);

#endif // MEMORY_COMMANDS_H