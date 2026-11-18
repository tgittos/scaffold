#ifndef SLASH_COMMANDS_H
#define SLASH_COMMANDS_H

#include <stddef.h>
#include <stdbool.h>

/** Returned by slash_command_dispatch when the command is not found. */
#define SLASH_NOT_FOUND (-2)

typedef struct AgentSession AgentSession;

typedef int (*SlashCommandHandler)(const char *args, AgentSession *session);

/**
 * Public slash command descriptor (name + description only).
 * Used by print_help to list available commands without coupling to handlers.
 */
typedef struct {
    const char *name;
    const char *description;
} SlashCommandInfo;

/** Read-only table of built-in slash commands, populated at compile time. */
extern const SlashCommandInfo SLASH_COMMAND_TABLE[];
extern const size_t SLASH_COMMAND_COUNT;

int slash_command_register(const char *name, const char *description,
                           SlashCommandHandler handler);
int slash_command_dispatch(const char *line, AgentSession *session);
void slash_commands_init(AgentSession *session);
void slash_commands_cleanup(void);

/**
 * Returns true when a REPL input line should be forwarded to the LLM.
 * Lines starting with '/' are slash commands and are never forwarded.
 */
bool repl_should_forward_to_llm(const char *line);

#endif /* SLASH_COMMANDS_H */
