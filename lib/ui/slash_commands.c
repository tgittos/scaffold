#include "slash_commands.h"
#include "memory_commands.h"
#include "mode_commands.h"
#include "model_commands.h"
#include "task_commands.h"
#include "agent_commands.h"
#include "goal_commands.h"
#include "terminal.h"
#include "../agent/session.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MAX_SLASH_COMMANDS 16
#define BUILTIN_COMMAND_COUNT 8

typedef struct {
    const char *name;
    const char *description;
    SlashCommandHandler handler;
} SlashCommand;

static SlashCommand g_commands[MAX_SLASH_COMMANDS];
static int g_command_count = 0;

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
    return -1;
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

void slash_commands_init(AgentSession *session) {
    (void)session;
    _Static_assert(BUILTIN_COMMAND_COUNT <= MAX_SLASH_COMMANDS,
                   "Too many built-in commands for registry");
    g_command_count = 0;
    int rc = 0;
    rc |= slash_command_register("help", "Show available commands", handle_help);
    rc |= slash_command_register("memory", "Manage semantic memories", process_memory_command);
    rc |= slash_command_register("model", "Switch AI models", process_model_command);
    rc |= slash_command_register("tasks", "View and manage tasks", process_task_command);
    rc |= slash_command_register("mode", "Switch behavioral mode", process_mode_command);
    rc |= slash_command_register("agents", "View subagent status", process_agent_command);
    rc |= slash_command_register("goals", "View GOAP goals and actions", process_goals_command);
    rc |= slash_command_register("plugins", "Show loaded plugins", handle_plugins);
    assert(rc == 0 && "Failed to register built-in slash commands");
    (void)rc;
}

void slash_commands_cleanup(void) {
    g_command_count = 0;
}
