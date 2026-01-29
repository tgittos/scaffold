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

struct GatePrompter {
    int is_interactive;     /* 1 if we have an interactive TTY */
    struct termios original_termios;  /* Original terminal settings */
    int termios_saved;      /* 1 if we've saved terminal settings */
};

/* Global interrupt flag for signal handler */
static volatile sig_atomic_t g_prompter_interrupted = 0;

static void prompter_sigint_handler(int sig) {
    (void)sig;
    g_prompter_interrupted = 1;
}

GatePrompter *gate_prompter_create(void) {
    if (!isatty(STDIN_FILENO)) {
        return NULL;
    }

    GatePrompter *gp = calloc(1, sizeof(GatePrompter));
    if (gp == NULL) {
        return NULL;
    }

    gp->is_interactive = 1;

    if (tcgetattr(STDIN_FILENO, &gp->original_termios) == 0) {
        gp->termios_saved = 1;
    }

    return gp;
}

void gate_prompter_destroy(GatePrompter *gp) {
    if (gp == NULL) {
        return;
    }

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

int gate_prompter_read_key(GatePrompter *gp) {
    if (gp == NULL || !gp->is_interactive) {
        return -1;
    }

    struct termios new_termios;
    int have_termios = 0;

    struct sigaction sa, old_sa;
    sa.sa_handler = prompter_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* No SA_RESTART - we want read() to be interrupted */
    sigaction(SIGINT, &sa, &old_sa);
    g_prompter_interrupted = 0;

    if (gp->termios_saved) {
        new_termios = gp->original_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 1;   /* Wait for at least 1 character */
        new_termios.c_cc[VTIME] = 0;  /* No timeout */

        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
            have_termios = 1;
        }
    }

    char c;
    int ch = -1;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1 && !g_prompter_interrupted) {
        ch = (unsigned char)c;
    }

    if (have_termios && gp->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &gp->original_termios);
    }

    sigaction(SIGINT, &old_sa, NULL);

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

    if (gp->termios_saved) {
        new_termios = gp->original_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        new_termios.c_cc[VMIN] = 0;
        new_termios.c_cc[VTIME] = (timeout_ms + 99) / 100; /* deciseconds */
        if (new_termios.c_cc[VTIME] < 1) {
            new_termios.c_cc[VTIME] = 1;
        }
        if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) == 0) {
            have_termios = 1;
        }
    }

    char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    int result = 0;
    if (n == 1) {
        *pressed_key = c;
        result = 1;
    } else if (n == 0) {
        result = 0;
    } else {
        result = -1;
    }

    if (have_termios && gp->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &gp->original_termios);
    }

    return result;
}

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

void gate_prompter_clear_prompt(GatePrompter *gp) {
    if (gp == NULL) {
        return;
    }
    /* Move cursor up 3 lines and clear to end of screen.
     * The prompt consists of:
     *   1. Empty line (from approval_gate_prompt)
     *   2. "● tool detail" line
     *   3. "  └─ Allow? [y/n/a/?] <response>" line
     */
    fprintf(stderr, "\033[3A\033[J");
    fflush(stderr);
}

void gate_prompter_clear_batch_prompt(GatePrompter *gp, int count) {
    if (gp == NULL || count <= 0) {
        return;
    }
    /* Move cursor up (count + 3) lines and clear to end of screen.
     * The batch prompt consists of:
     *   1. Empty line (from approval_gate_prompt_batch)
     *   2. "● N operations" header line
     *   3-N+2. One line per operation
     *   N+3. "  └─ Allow all? [y/n/1-N] <response>" line
     */
    int lines_to_clear = count + 3;
    fprintf(stderr, "\033[%dA\033[J", lines_to_clear);
    fflush(stderr);
}

void gate_prompter_show_single(GatePrompter *gp,
                               const ToolCall *tool_call,
                               const char *command,
                               const char *path) {
    if (gp == NULL || tool_call == NULL) {
        return;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

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

    /* Clear any existing prompt (e.g., readline "> ") before showing gate */
    fprintf(stderr, "\r\033[K");

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

    fprintf(stderr, "\n● %s details\n", tool_name);
    fprintf(stderr, "  ├─ tool: %s\n", tool_name);

    if (tool_call->arguments != NULL && strlen(tool_call->arguments) > 0) {
        fprintf(stderr, "  ├─ args:" ANSI_DIM " %s" ANSI_RESET "\n", tool_call->arguments);
    }

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

    /* Clear any existing prompt (e.g., readline "> ") before showing gate */
    fprintf(stderr, "\r\033[K");

    fprintf(stderr, "● %d operations\n", count);

    for (int i = 0; i < count; i++) {
        char status_char = (statuses != NULL) ? statuses[i] : ' ';
        const char *name = tool_calls[i].name ? tool_calls[i].name : "unknown";
        const char *connector = (i == count - 1) ? "└─" : "├─";

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
