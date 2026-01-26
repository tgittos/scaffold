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

    fprintf(stderr, "\n");
    fprintf(stderr, "\xe2\x94\x8c\xe2\x94\x80 Approval Required \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n");
    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");
    fprintf(stderr, "\xe2\x94\x82  Tool: %-53s \xe2\x94\x82\n", tool_call->name ? tool_call->name : "unknown");

    /* Show command or path if available */
    if (command != NULL && strlen(command) > 0) {
        char cmd_display[52];
        size_t cmd_len = strlen(command);
        if (cmd_len <= 50) {
            snprintf(cmd_display, sizeof(cmd_display), "%s", command);
        } else {
            snprintf(cmd_display, sizeof(cmd_display), "%.47s...", command);
        }
        fprintf(stderr, "\xe2\x94\x82  Command: %-50s \xe2\x94\x82\n", cmd_display);
    } else if (path != NULL && strlen(path) > 0) {
        char path_display[54];
        size_t path_len = strlen(path);
        if (path_len <= 53) {
            snprintf(path_display, sizeof(path_display), "%s", path);
        } else {
            snprintf(path_display, sizeof(path_display), "...%s", path + path_len - 50);
        }
        fprintf(stderr, "\xe2\x94\x82  Path: %-53s \xe2\x94\x82\n", path_display);
    } else if (tool_call->arguments != NULL) {
        char arg_display[54];
        size_t arg_len = strlen(tool_call->arguments);
        if (arg_len <= 53) {
            snprintf(arg_display, sizeof(arg_display), "%s", tool_call->arguments);
        } else {
            snprintf(arg_display, sizeof(arg_display), "%.50s...", tool_call->arguments);
        }
        fprintf(stderr, "\xe2\x94\x82  Args: %-53s \xe2\x94\x82\n", arg_display);
    }

    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");
    fprintf(stderr, "\xe2\x94\x82  [y] Allow  [n] Deny  [a] Allow always  [?] Details          \xe2\x94\x82\n");
    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");
    fprintf(stderr, "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n");
    fprintf(stderr, "> ");
    fflush(stderr);
}

void gate_prompter_show_details(GatePrompter *gp,
                                const ToolCall *tool_call,
                                const char *resolved_path,
                                int path_exists) {
    if (gp == NULL || tool_call == NULL) {
        return;
    }

    fprintf(stderr, "\n");
    fprintf(stderr, "\xe2\x94\x8c\xe2\x94\x80 Details \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n");

    /* Tool name */
    fprintf(stderr, "\xe2\x94\x82  Tool: %-53s \xe2\x94\x82\n", tool_call->name ? tool_call->name : "unknown");
    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");

    /* Full arguments */
    fprintf(stderr, "\xe2\x94\x82  Full arguments:                                             \xe2\x94\x82\n");
    if (tool_call->arguments != NULL && strlen(tool_call->arguments) > 0) {
        /* Split arguments into lines for display */
        const char *args = tool_call->arguments;
        size_t args_len = strlen(args);

        if (args_len <= 56) {
            fprintf(stderr, "\xe2\x94\x82    %-56s \xe2\x94\x82\n", args);
        } else {
            /* Display first 56 chars with ellipsis */
            fprintf(stderr, "\xe2\x94\x82    %.53s... \xe2\x94\x82\n", args);
        }
    } else {
        fprintf(stderr, "\xe2\x94\x82    (none)                                                    \xe2\x94\x82\n");
    }

    /* Resolved path if available */
    if (resolved_path != NULL && strlen(resolved_path) > 0) {
        fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");
        fprintf(stderr, "\xe2\x94\x82  Resolved path:                                              \xe2\x94\x82\n");
        char path_display[57];
        size_t path_len = strlen(resolved_path);
        if (path_len <= 56) {
            snprintf(path_display, sizeof(path_display), "%s", resolved_path);
        } else {
            snprintf(path_display, sizeof(path_display), "...%s", resolved_path + path_len - 53);
        }
        fprintf(stderr, "\xe2\x94\x82    %-56s \xe2\x94\x82\n", path_display);

        if (path_exists) {
            fprintf(stderr, "\xe2\x94\x82    (existing file)                                           \xe2\x94\x82\n");
        } else {
            fprintf(stderr, "\xe2\x94\x82    (new file)                                                \xe2\x94\x82\n");
        }
    }

    fprintf(stderr, "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n");
    fprintf(stderr, "\nPress any key to return to prompt...\n");
    fflush(stderr);
}

void gate_prompter_show_batch(GatePrompter *gp,
                              const ToolCall *tool_calls,
                              int count,
                              const char *statuses) {
    if (gp == NULL || tool_calls == NULL || count <= 0) {
        return;
    }

    fprintf(stderr, "\n");
    fprintf(stderr, "\xe2\x94\x8c\xe2\x94\x80 Approval Required (%d operations) ", count);

    /* Fill the rest of the top border */
    int title_len = 24 + (count >= 10 ? 2 : 1);  /* "Approval Required (N operations) " */
    for (int i = title_len; i < 62; i++) {
        fprintf(stderr, "\xe2\x94\x80");
    }
    fprintf(stderr, "\xe2\x94\x90\n");

    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");

    /* List each operation */
    for (int i = 0; i < count; i++) {
        char line[128];  /* Large enough for any formatting */
        char status_char = (statuses != NULL) ? statuses[i] : ' ';

        /* Format: N. tool_name: first_arg_preview */
        const char *name = tool_calls[i].name ? tool_calls[i].name : "unknown";
        if (tool_calls[i].arguments != NULL) {
            size_t arg_len = strlen(tool_calls[i].arguments);
            if (arg_len <= 40) {
                snprintf(line, sizeof(line), "%d. %s: %s",
                         i + 1, name, tool_calls[i].arguments);
            } else {
                /* Truncate and add ellipsis */
                char arg_preview[44];
                memcpy(arg_preview, tool_calls[i].arguments, 40);
                arg_preview[40] = '.';
                arg_preview[41] = '.';
                arg_preview[42] = '.';
                arg_preview[43] = '\0';
                snprintf(line, sizeof(line), "%d. %s: %s", i + 1, name, arg_preview);
            }
        } else {
            snprintf(line, sizeof(line), "%d. %s", i + 1, name);
        }
        /* Ensure line fits in display width */
        if (strlen(line) > 54) {
            line[51] = '.';
            line[52] = '.';
            line[53] = '.';
            line[54] = '\0';
        }

        if (status_char != ' ') {
            fprintf(stderr, "\xe2\x94\x82  [%c] %-54s \xe2\x94\x82\n", status_char, line);
        } else {
            fprintf(stderr, "\xe2\x94\x82  %-58s \xe2\x94\x82\n", line);
        }
    }

    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");

    if (count <= 9) {
        fprintf(stderr, "\xe2\x94\x82  [y] Allow all  [n] Deny all  [1-%d] Review individual       \xe2\x94\x82\n", count);
    } else {
        fprintf(stderr, "\xe2\x94\x82  [y] Allow all  [n] Deny all  [1-%d] Review individual      \xe2\x94\x82\n", count);
    }

    fprintf(stderr, "\xe2\x94\x82                                                              \xe2\x94\x82\n");
    fprintf(stderr, "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n");
    fprintf(stderr, "> ");
    fflush(stderr);
}
