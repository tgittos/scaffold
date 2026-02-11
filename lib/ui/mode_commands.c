#include "mode_commands.h"
#include "../agent/session.h"
#include "../agent/prompt_mode.h"
#include "../util/ansi_codes.h"
#include "status_line.h"
#include <stdio.h>
#include <string.h>

static void show_current_mode(AgentSession *session) {
    const char *name = prompt_mode_name(session->current_mode);
    const char *desc = prompt_mode_description(session->current_mode);
    printf(TERM_BOLD "Current mode:" TERM_RESET " %s — %s\n", name, desc);
}

static void show_mode_list(AgentSession *session) {
    printf(TERM_BOLD "Available modes:" TERM_RESET "\n");
    for (int i = 0; i < PROMPT_MODE_COUNT; i++) {
        const char *active = (session->current_mode == (PromptMode)i) ? " (active)" : "";
        printf("  %-10s %s%s\n",
               prompt_mode_name((PromptMode)i),
               prompt_mode_description((PromptMode)i),
               active);
    }
}

static void switch_mode(const char *name, AgentSession *session) {
    PromptMode new_mode;
    if (prompt_mode_from_name(name, &new_mode) != 0) {
        printf("Unknown mode '%s'. Use /mode list to see available modes.\n", name);
        return;
    }

    session->current_mode = new_mode;
    status_line_set_mode(new_mode);
    printf("Switched to " TERM_BOLD "%s" TERM_RESET " mode — %s\n",
           prompt_mode_name(new_mode), prompt_mode_description(new_mode));
}

int process_mode_command(const char *args, AgentSession *session) {
    if (args == NULL || session == NULL) return -1;

    if (*args == '\0') {
        show_current_mode(session);
        return 0;
    }

    if (strcmp(args, "list") == 0) {
        show_mode_list(session);
        return 0;
    }

    switch_mode(args, session);
    return 0;
}
