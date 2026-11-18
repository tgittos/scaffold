#include "slash_commands.h"
#include "memory_commands.h"
#include "mode_commands.h"
#include "model_commands.h"
#include "recap_command.h"
#include "task_commands.h"
#include "agent_commands.h"
#include "goal_commands.h"
#include "terminal.h"
#include "../agent/session.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MAX_SLASH_COMMANDS 16

typedef struct {
    const char *name;
    const char *description;
    SlashCommandHandler handler;
} SlashCommand;

static SlashCommand g_commands[MAX_SLASH_COMMANDS];
static int g_command_count = 0;

/*
 * Public compile-time table of built-in slash commands (name + description).
 * print_help iterates this to show the Interactive Mode Commands section.
 * slash_commands_init pairs each entry with the corresponding handler below.
 */
const SlashCommandInfo SLASH_COMMAND_TABLE[] = {
    {"help",    "Show available commands"},
    {"memory",  "Manage semantic memories"},
    {"model",   "Switch AI models"},
    {"recap",   "Show a recap of recent conversation"},
    {"tasks",   "View and manage tasks"},
    {"mode",    "Switch behavioral mode"},
    {"agents",  "View subagent status"},
    {"goals",   "View GOAP goals and actions"},
    {"plugins", "Show loaded plugins"},
};
const size_t SLASH_COMMAND_COUNT =
    sizeof(SLASH_COMMAND_TABLE) / sizeof(SLASH_COMMAND_TABLE[0]);

int slash_command_register(const char *name, const char *description,
                           SlashCommandHandler handler) {
    if (g_command_count >= MAX_SLASH_COMMANDS) {
        return -1;
    }
    g_commands[g_command_count].name = name;
    g_commands[g_command_count].description = description;
    g_commands[g_command_count].handler = handler;
    g_command_count++;
    return 0;
}

int slash_command_dispatch(const char *line, AgentSession *session) {
    if (line == NULL || line[0] != '/') {
        return -1;
    }

    const char *cmd_start = line + 1;
    for (int i = 0; i < g_command_count; i++) {
        size_t name_len = strlen(g_commands[i].name);
        if (strncmp(cmd_start, g_commands[i].name, name_len) == 0 &&
            (cmd_start[name_len] == '\0' || cmd_start[name_len] == ' ')) {
            const char *args = cmd_start + name_len;
            while (*args == ' ') {
                args++;
            }
            return g_commands[i].handler(args, session);
        }
    }
    /* Print only the command token (stop at the first space) */
    char cmd_token[64];
    size_t tok_len = 0;
    while (line[tok_len] && line[tok_len] != ' ' && tok_len < sizeof(cmd_token) - 1) {
        cmd_token[tok_len] = line[tok_len];
        tok_len++;
    }
    cmd_token[tok_len] = '\0';
    fprintf(stderr, "Unknown command '%s'. Type /help for commands.\n", cmd_token);
    return SLASH_NOT_FOUND;
}

bool repl_should_forward_to_llm(const char *line) {
    return line != NULL && line[0] != '/';
}

static int handle_help(const char *args, AgentSession *session) {
    (void)args;
    (void)session;
    printf("\n" TERM_BOLD "Available Commands" TERM_RESET "\n");
    printf(TERM_SEP_LIGHT_40 "\n");
    for (int i = 0; i < g_command_count; i++) {
        printf("  " TERM_BOLD "/%-10s" TERM_RESET "  %s\n",
               g_commands[i].name, g_commands[i].description);
    }
    printf("\n");
    return 0;
}

static int handle_plugins(const char *args, AgentSession *session) {
    (void)args;
    if (!session) {
        printf("  No session available\n");
        return 0;
    }

    PluginManager *mgr = &session->plugin_manager;
    printf("\n" TERM_BOLD "Plugins" TERM_RESET "\n");
    printf(TERM_SEP_LIGHT_40 "\n");

    if (mgr->count == 0) {
        printf("  No plugins loaded\n");
        char *dir = plugin_manager_get_plugins_dir();
        if (dir) {
            printf("  Directory: %s\n", dir);
            free(dir);
        }
        printf("\n");
        return 0;
    }

    for (int i = 0; i < mgr->count; i++) {
        PluginProcess *p = &mgr->plugins[i];
        if (!p->initialized) continue;

        printf("  " TERM_BOLD "%s" TERM_RESET " v%s",
               p->manifest.name, p->manifest.version);
        if (p->manifest.description && p->manifest.description[0]) {
            printf(" - %s", p->manifest.description);
        }
        printf("\n");
        printf("    PID: %d  Priority: %d\n", p->pid, p->manifest.priority);

        if (p->manifest.hook_count > 0) {
            printf("    Hooks: ");
            for (int j = 0; j < p->manifest.hook_count; j++) {
                printf("%s%s", j > 0 ? ", " : "", p->manifest.hooks[j]);
            }
            printf("\n");
        }

        if (p->manifest.tool_count > 0) {
            printf("    Tools: ");
            for (int j = 0; j < p->manifest.tool_count; j++) {
                printf("%s%s", j > 0 ? ", " : "", p->manifest.tools[j].name);
            }
            printf("\n");
        }
    }
    printf("\n");
    return 0;
}

/*
 * Handlers paired with SLASH_COMMAND_TABLE by index.
 * Must stay in sync with SLASH_COMMAND_TABLE order.
 */
static const SlashCommandHandler g_builtin_handlers[] = {
    handle_help,
    process_memory_command,
    process_model_command,
    process_recap_command,
    process_task_command,
    process_mode_command,
    process_agent_command,
    process_goals_command,
    handle_plugins,
};

_Static_assert(
    sizeof(g_builtin_handlers) / sizeof(g_builtin_handlers[0]) ==
    sizeof(SLASH_COMMAND_TABLE) / sizeof(SLASH_COMMAND_TABLE[0]),
    "g_builtin_handlers must have the same number of entries as SLASH_COMMAND_TABLE");

void slash_commands_init(AgentSession *session) {
    (void)session;
    g_command_count = 0;
    int rc = 0;
    for (size_t i = 0; i < SLASH_COMMAND_COUNT; i++) {
        rc |= slash_command_register(SLASH_COMMAND_TABLE[i].name,
                                     SLASH_COMMAND_TABLE[i].description,
                                     g_builtin_handlers[i]);
    }
    assert(rc == 0 && "Failed to register built-in slash commands");
    (void)rc;
}

void slash_commands_cleanup(void) {
    g_command_count = 0;
}
