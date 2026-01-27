/**
 * Gate Prompter Implementation
 *
 * Handles all terminal UI for the approval gate system.
 * Encapsulates TTY detection, terminal mode switching, signal handling,
 * and display formatting.
 */

#include "gate_prompter.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/* ANSI color codes - matching output_formatter.h */
#define ANSI_RESET   "\033[0m"
#define ANSI_DIM     "\033[2m"
#define ANSI_GRAY    "\033[90m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BOLD    "\033[1m"

/* =============================================================================
 * Internal Data Structures
 * ========================================================================== */

struct GatePrompter {
    int is_interactive;     /* 1 if we have an interactive TTY */
    struct termios original_termios;  /* Original terminal settings */
    int termios_saved;      /* 1 if we've saved terminal settings */
};

/* Global interrupt flag for signal handler */
static volatile sig_atomic_t g_prompter_interrupted = 0;

/* =============================================================================
 * Signal Handling
 * ========================================================================== */

/**
 * Signal handler for Ctrl+C during prompting.
 */
static void prompter_sigint_handler(int sig) {
    (void)sig;
    g_prompter_interrupted = 1;
}

/* =============================================================================
 * Public API - Lifecycle
 * ========================================================================== */

GatePrompter *gate_prompter_create(void) {
    if (!isatty(STDIN_FILENO)) {
        return NULL;
    }

    GatePrompter *gp = calloc(1, sizeof(GatePrompter));
    if (gp == NULL) {
        return NULL;
    }

    gp->is_interactive = 1;

    /* Save original terminal settings */
    if (tcgetattr(STDIN_FILENO, &gp->original_termios) == 0) {
        gp->termios_saved = 1;
    }

    return gp;
}

void gate_prompter_destroy(GatePrompter *gp) {
    if (gp == NULL) {
        return;
    }

    /* Restore terminal settings if we modified them */
    if (gp->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &gp->original_termios);
    }

    free(gp);
}

int gate_prompter_is_interactive(const GatePrompter *gp) {
    if (gp == NULL) {
        return 0;
    }
    return gp->is_interactive;
}

/* =============================================================================
 * Public API - Input
 * ========================================================================== */

int gate_prompter_read_key(GatePrompter *gp) {
    if (gp == NULL || !gp->is_interactive) {
        return -1;
    }

    struct termios new_termios;
    int have_termios = 0;

    /* Set up Ctrl+C handler */
    struct sigaction sa, old_sa;
    sa.sa_handler = prompter_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* No SA_RESTART - we want read() to be interrupted */
    sigaction(SIGINT, &sa, &old_sa);
    g_prompter_interrupted = 0;

    /* Set up raw mode: disable canonical mode and echo */
    if (gp->termios_saved) {
        new_termios = gp->original_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 1;   /* Wait for at least 1 character */
        new_termios.c_cc[VTIME] = 0;  /* No timeout */

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
            have_termios = 1;
        }
    }

    /* Read single character */
    char c;
    int ch = -1;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1 && !g_prompter_interrupted) {
        ch = (unsigned char)c;
    }

    /* Restore terminal settings */
    if (have_termios && gp->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &gp->original_termios);
    }

    /* Restore signal handler */
    sigaction(SIGINT, &old_sa, NULL);

    /* Check if interrupted */
    if (g_prompter_interrupted) {
        return -1;
    }

    return ch;
}

int gate_prompter_read_key_timeout(GatePrompter *gp, int timeout_ms, char *pressed_key) {
    if (gp == NULL || !gp->is_interactive || pressed_key == NULL) {
        return -1;
    }

    struct termios new_termios;
    int have_termios = 0;

    /* Set up raw mode with timeout */
    if (gp->termios_saved) {
        new_termios = gp->original_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 0;
        /* Convert ms to deciseconds (100ms units) */
        new_termios.c_cc[VTIME] = (timeout_ms + 99) / 100;
        if (new_termios.c_cc[VTIME] < 1) {
            new_termios.c_cc[VTIME] = 1;
        }
        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
            have_termios = 1;
        }
    }

    /* Read with timeout */
    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    int result = 0;
    if (n == 1) {
        *pressed_key = c;
        result = 1;
    } else if (n == 0) {
        result = 0;  /* Timeout */
    } else {
        result = -1; /* Error */
    }

    /* Restore terminal settings */
    if (have_termios && gp->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &gp->original_termios);
    }

    return result;
}

/* =============================================================================
 * Public API - Output
 * ========================================================================== */

void gate_prompter_print(GatePrompter *gp, const char *format, ...) {
    if (gp == NULL || format == NULL) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fflush(stderr);
}

void gate_prompter_newline(GatePrompter *gp) {
    if (gp == NULL) {
        return;
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* =============================================================================
 * Public API - Display Functions
 * ========================================================================== */

void gate_prompter_show_single(GatePrompter *gp,
                               const ToolCall *tool_call,
                               const char *command,
                               const char *path) {
    if (gp == NULL || tool_call == NULL) {
        return;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    /* Build detail string (command, path, or truncated args) */
    char detail[80] = "";
    if (command != NULL && strlen(command) > 0) {
        size_t cmd_len = strlen(command);
        if (cmd_len <= 60) {
            snprintf(detail, sizeof(detail), "%s", command);
        } else {
            snprintf(detail, sizeof(detail), "%.57s...", command);
        }
    } else if (path != NULL && strlen(path) > 0) {
        size_t path_len = strlen(path);
        if (path_len <= 60) {
            snprintf(detail, sizeof(detail), "%s", path);
        } else {
            snprintf(detail, sizeof(detail), "...%s", path + path_len - 57);
        }
    } else if (tool_call->arguments != NULL && strlen(tool_call->arguments) > 0) {
        size_t arg_len = strlen(tool_call->arguments);
        if (arg_len <= 60) {
            snprintf(detail, sizeof(detail), "%s", tool_call->arguments);
        } else {
            snprintf(detail, sizeof(detail), "%.57s...", tool_call->arguments);
        }
    }

    /* Compact tree-style format */
    fprintf(stderr, "● %s", tool_name);
    if (strlen(detail) > 0) {
        fprintf(stderr, ANSI_DIM " %s" ANSI_RESET, detail);
    }
    fprintf(stderr, "\n");
    fprintf(stderr, "  └─ Allow? [y/n/a/?] ");
    fflush(stderr);
}

void gate_prompter_show_details(GatePrompter *gp,
                                const ToolCall *tool_call,
                                const char *resolved_path,
                                int path_exists) {
    if (gp == NULL || tool_call == NULL) {
        return;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    /* Tree-style details view */
    fprintf(stderr, "\n● %s details\n", tool_name);
    fprintf(stderr, "  ├─ tool: %s\n", tool_name);

    /* Full arguments */
    if (tool_call->arguments != NULL && strlen(tool_call->arguments) > 0) {
        fprintf(stderr, "  ├─ args:" ANSI_DIM " %s" ANSI_RESET "\n", tool_call->arguments);
    }

    /* Resolved path if available */
    if (resolved_path != NULL && strlen(resolved_path) > 0) {
        fprintf(stderr, "  ├─ path:" ANSI_DIM " %s", resolved_path);
        if (path_exists) {
            fprintf(stderr, " (exists)");
        } else {
            fprintf(stderr, " (new)");
        }
        fprintf(stderr, ANSI_RESET "\n");
    }

    fprintf(stderr, "  └─" ANSI_DIM " Press any key..." ANSI_RESET "\n");
    fflush(stderr);
}

void gate_prompter_show_batch(GatePrompter *gp,
                              const ToolCall *tool_calls,
                              int count,
                              const char *statuses) {
    if (gp == NULL || tool_calls == NULL || count <= 0) {
        return;
    }

    /* Tree-style batch format */
    fprintf(stderr, "● %d operations\n", count);

    /* List each operation with tree connectors */
    for (int i = 0; i < count; i++) {
        char status_char = (statuses != NULL) ? statuses[i] : ' ';
        const char *name = tool_calls[i].name ? tool_calls[i].name : "unknown";
        const char *connector = (i == count - 1) ? "└─" : "├─";

        /* Build truncated argument preview */
        char arg_preview[50] = "";
        if (tool_calls[i].arguments != NULL && strlen(tool_calls[i].arguments) > 0) {
            size_t arg_len = strlen(tool_calls[i].arguments);
            if (arg_len <= 45) {
                snprintf(arg_preview, sizeof(arg_preview), " %s", tool_calls[i].arguments);
            } else {
                snprintf(arg_preview, sizeof(arg_preview), " %.42s...", tool_calls[i].arguments);
            }
        }

        if (status_char != ' ') {
            fprintf(stderr, "  %s [%c] %s" ANSI_DIM "%s" ANSI_RESET "\n",
                    connector, status_char, name, arg_preview);
        } else {
            fprintf(stderr, "  %s %s" ANSI_DIM "%s" ANSI_RESET "\n",
                    connector, name, arg_preview);
        }
    }

    fprintf(stderr, "  └─ Allow all? [y/n/1-%d] ", count);
    fflush(stderr);
}
