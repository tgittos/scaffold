#include "gate_prompter.h"
#include "../util/ansi_codes.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

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
    /* Move cursor up 2 lines and clear to end of screen.
     * The prompt consists of:
     *   1. "● tool detail" line
     *   2. "  └─ Allow? [y/n/?] <response>" line
     */
    fprintf(stderr, TERM_CURSOR_UP_FMT TERM_CLEAR_SCREEN, 2);
    fflush(stderr);
}

void gate_prompter_clear_batch_prompt(GatePrompter *gp, int count) {
    if (gp == NULL || count <= 0) {
        return;
    }
    /* Move cursor up (count + 2) lines and clear to end of screen.
     * The batch prompt consists of:
     *   1. "● N operations" header line
     *   2-N+1. One line per operation
     *   N+2. "  └─ Allow all? [y/n/1-N] <response>" line
     */
    int lines_to_clear = count + 2;
    fprintf(stderr, TERM_CURSOR_UP_FMT TERM_CLEAR_SCREEN, lines_to_clear);
    fflush(stderr);
}

/* Longest escape an input character turns into, plus the terminator. */
#define ESCAPE_MAX 10

/*
 * Code points that reorder or hide the text around them: the bidi overrides,
 * embeddings and isolates, the left/right-to-right marks, and the C1 block,
 * which terminals read as escape sequences.
 */
static int codepoint_is_deceptive(unsigned long cp) {
    return (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2066 && cp <= 0x2069) ||
           cp == 0x200E || cp == 0x200F || (cp >= 0x0080 && cp <= 0x009F);
}

/*
 * Decode the UTF-8 sequence at c. Returns its length in bytes, or 0 when the
 * bytes are not a well-formed, shortest-form sequence.
 */
static size_t utf8_decode(const unsigned char *c, unsigned long *out_cp) {
    size_t len;
    unsigned long cp;
    if ((c[0] & 0xE0) == 0xC0) {
        len = 2;
        cp = c[0] & 0x1Fu;
    } else if ((c[0] & 0xF0) == 0xE0) {
        len = 3;
        cp = c[0] & 0x0Fu;
    } else if ((c[0] & 0xF8) == 0xF0) {
        len = 4;
        cp = c[0] & 0x07u;
    } else {
        return 0;
    }
    for (size_t i = 1; i < len; i++) {
        if ((c[i] & 0xC0) != 0x80) {
            return 0;
        }
        cp = (cp << 6) | (c[i] & 0x3Fu);
    }
    static const unsigned long MIN_FOR_LEN[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < MIN_FOR_LEN[len] || cp > 0x10FFFF) {
        return 0;
    }
    *out_cp = cp;
    return len;
}

/*
 * How the character at c is shown: itself, or an escape. Returns the length
 * written and the number of input bytes it stands for.
 */
static size_t escape_char_at(char out[ESCAPE_MAX], const unsigned char *c,
                             size_t *out_consumed) {
    *out_consumed = 1;
    if (c[0] == '\n' || c[0] == '\t' || c[0] == '\r') {
        out[0] = '\\';
        out[1] = c[0] == '\n' ? 'n' : (c[0] == '\t' ? 't' : 'r');
        out[2] = '\0';
        return 2;
    }
    if (c[0] < 0x20 || c[0] == 0x7f) {
        snprintf(out, ESCAPE_MAX, "\\x%02x", c[0]);
        return 4;
    }
    if (c[0] >= 0x80) {
        unsigned long cp = 0;
        size_t len = utf8_decode(c, &cp);
        if (len > 0 && codepoint_is_deceptive(cp)) {
            snprintf(out, ESCAPE_MAX, "\\u{%04lX}", cp);
            *out_consumed = len;
            return 8;
        }
    }
    out[0] = (char)c[0];
    out[1] = '\0';
    return 1;
}

/* Start offset of the character whose last byte is text[end - 1]. */
static size_t char_start(const unsigned char *text, size_t end) {
    size_t limit = end >= 4 ? end - 4 : 0;
    size_t start = end - 1;
    while (start > limit && (text[start] & 0xC0) == 0x80) {
        start--;
    }
    unsigned long cp = 0;
    if (text[start] >= 0x80 && utf8_decode(text + start, &cp) == end - start) {
        return start;
    }
    return end - 1;
}

/*
 * Escape text into buf, stopping before an escape that doesn't fit in either
 * buf or limit. *out_complete tells whether all of the text was written.
 */
static size_t escape_into(char *buf, size_t size, const char *text, size_t limit,
                          int *out_complete) {
    size_t out = 0;
    int complete = 1;
    for (const unsigned char *c = (const unsigned char *)text;
         text != NULL && *c != '\0';) {
        char escape[ESCAPE_MAX];
        size_t consumed = 1;
        size_t len = escape_char_at(escape, c, &consumed);
        if (out + len + 1 > size || out + len > limit) {
            complete = 0;
            break;
        }
        memcpy(buf + out, escape, len);
        out += len;
        c += consumed;
    }
    buf[out] = '\0';
    if (out_complete != NULL) {
        *out_complete = complete;
    }
    return out;
}

void gate_prompter_escape_control(char *buf, size_t size, const char *text) {
    if (buf == NULL || size == 0) {
        return;
    }
    escape_into(buf, size, text, (size_t)-1, NULL);
}

void gate_prompter_escape_truncate(char *buf, size_t size, const char *text,
                                   size_t max_chars) {
    if (buf == NULL || size == 0) {
        return;
    }
    int complete = 0;
    size_t out = escape_into(buf, size, text, max_chars, &complete);
    if (complete) {
        return;
    }
    /* Make room for the ellipsis without cutting into an escape sequence. */
    size_t room = max_chars > 3 ? max_chars - 3 : 0;
    if (room > size - 4) {
        room = size > 4 ? size - 4 : 0;
    }
    if (out > room) {
        out = escape_into(buf, size, text, room, NULL);
    }
    snprintf(buf + out, size - out, "...");
}

char *gate_prompter_escape_alloc(const char *text) {
    if (text == NULL) {
        return NULL;
    }
    /* The longest escape, "\u{XXXX}", stands for at least two input bytes. */
    size_t size = strlen(text) * 4 + 1;
    char *buf = malloc(size);
    if (buf == NULL) {
        return NULL;
    }
    escape_into(buf, size, text, (size_t)-1, NULL);
    return buf;
}

/* The tail of a long value, escaped, with a leading "...". */
static void escape_tail(char *buf, size_t size, const char *text, size_t max_chars) {
    size_t len = strlen(text);
    size_t budget = max_chars > 3 ? max_chars - 3 : 0;
    size_t shown = 0;
    size_t start = len;
    while (start > 0) {
        size_t begin = char_start((const unsigned char *)text, start);
        char escape[ESCAPE_MAX];
        size_t consumed = 1;
        size_t piece = escape_char_at(escape, (const unsigned char *)text + begin, &consumed);
        if (shown + piece > budget) {
            break;
        }
        shown += piece;
        start = begin;
    }
    snprintf(buf, size, "...");
    escape_into(buf + 3, size > 3 ? size - 3 : 0, text + start, budget, NULL);
}

void gate_prompter_format_detail(char *buf, size_t size, const char *command,
                                 const char *path, const char *arguments,
                                 const char *working_dir) {
    enum { MAX_DETAIL = 60, MAX_DIR = 30 };
    if (buf == NULL || size == 0) {
        return;
    }
    buf[0] = '\0';

    char value[MAX_DETAIL + 4];
    value[0] = '\0';
    if (command != NULL && command[0] != '\0') {
        gate_prompter_escape_truncate(value, sizeof(value), command, MAX_DETAIL);
    } else if (path != NULL && path[0] != '\0') {
        char escaped[MAX_DETAIL + 4];
        int complete = 0;
        escape_into(escaped, sizeof(escaped), path, MAX_DETAIL, &complete);
        if (complete) {
            snprintf(value, sizeof(value), "%s", escaped);
        } else {
            escape_tail(value, sizeof(value), path, MAX_DETAIL);
        }
    } else if (arguments != NULL && arguments[0] != '\0') {
        gate_prompter_escape_truncate(value, sizeof(value), arguments, MAX_DETAIL);
    }

    if (working_dir == NULL || working_dir[0] == '\0') {
        snprintf(buf, size, "%s", value);
        return;
    }

    char dir[MAX_DIR + 4];
    int complete = 0;
    escape_into(dir, sizeof(dir), working_dir, MAX_DIR, &complete);
    if (!complete) {
        escape_tail(dir, sizeof(dir), working_dir, MAX_DIR);
    }
    if (value[0] == '\0') {
        snprintf(buf, size, "(in %s)", dir);
    } else {
        snprintf(buf, size, "%s (in %s)", value, dir);
    }
}

void gate_prompter_show_single(GatePrompter *gp,
                               const ToolCall *tool_call,
                               const char *command,
                               const char *path,
                               const char *always_label,
                               const char *working_dir) {
    if (gp == NULL || tool_call == NULL) {
        return;
    }

    const char *tool_name = tool_call->name ? tool_call->name : "unknown";

    char detail[128] = "";
    gate_prompter_format_detail(detail, sizeof(detail), command, path,
                                tool_call->arguments, working_dir);

    /* Clear any existing prompt (e.g., readline "> ") before showing gate */
    fprintf(stderr, TERM_CLEAR_LINE);

    fprintf(stderr, "● %s", tool_name);
    if (strlen(detail) > 0) {
        fprintf(stderr, TERM_DIM " %s" TERM_RESET, detail);
    }
    fprintf(stderr, "\n");
    char hint[256];
    gate_prompter_format_hint(hint, sizeof(hint), always_label);
    fprintf(stderr, "  └─ %s ", hint);
    fflush(stderr);
}

void gate_prompter_format_hint(char *buf, size_t size, const char *always_label) {
    if (buf == NULL || size == 0) {
        return;
    }
    if (always_label == NULL) {
        snprintf(buf, size, "Allow? [y/n/?]");
    } else {
        char escaped[256];
        gate_prompter_escape_control(escaped, sizeof(escaped), always_label);
        always_label = escaped;
        /* The prompt line must fit in 80 columns: 5 for "  └─ ", 1 trailing
         * space, 35 of fixed hint text and 1 closing paren. The label carries
         * its own quotes. */
        enum { MAX_LABEL_CHARS = 80 - 6 - 35 };
        size_t chars = 0;
        const char *p = always_label;
        const char *cut = always_label;
        for (; *p; p++) {
            if (((unsigned char)*p & 0xC0) == 0x80) {
                continue;
            }
            if (chars == MAX_LABEL_CHARS - 2) {
                cut = p;
            }
            chars++;
        }
        if (chars <= MAX_LABEL_CHARS) {
            snprintf(buf, size, "Allow? [y/n/?] (a = always allow %s)", always_label);
        } else {
            snprintf(buf, size, "Allow? [y/n/?] (a = always allow %.*s…\")",
                     (int)(cut - always_label), always_label);
        }
    }
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
        char *args = gate_prompter_escape_alloc(tool_call->arguments);
        fprintf(stderr, "  ├─ args:" TERM_DIM " %s" TERM_RESET "\n",
                args != NULL ? args : "(unprintable)");
        free(args);
    }

    if (resolved_path != NULL && strlen(resolved_path) > 0) {
        char *safe_path = gate_prompter_escape_alloc(resolved_path);
        fprintf(stderr, "  ├─ path:" TERM_DIM " %s",
                safe_path != NULL ? safe_path : "(unprintable)");
        free(safe_path);
        if (path_exists) {
            fprintf(stderr, " (exists)");
        } else {
            fprintf(stderr, " (new)");
        }
        fprintf(stderr, TERM_RESET "\n");
    }

    fprintf(stderr, "  └─" TERM_DIM " Press any key..." TERM_RESET "\n");
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
    fprintf(stderr, TERM_CLEAR_LINE);

    fprintf(stderr, "● %d operations\n", count);

    for (int i = 0; i < count; i++) {
        char status_char = (statuses != NULL) ? statuses[i] : ' ';
        const char *name = tool_calls[i].name ? tool_calls[i].name : "unknown";
        const char *connector = (i == count - 1) ? "└─" : "├─";

        char arg_preview[50] = "";
        if (tool_calls[i].arguments != NULL && strlen(tool_calls[i].arguments) > 0) {
            arg_preview[0] = ' ';
            gate_prompter_escape_truncate(arg_preview + 1, sizeof(arg_preview) - 1,
                                          tool_calls[i].arguments, 45);
        }

        if (status_char != ' ') {
            fprintf(stderr, "  %s [%c] %s" TERM_DIM "%s" TERM_RESET "\n",
                    connector, status_char, name, arg_preview);
        } else {
            fprintf(stderr, "  %s %s" TERM_DIM "%s" TERM_RESET "\n",
                    connector, name, arg_preview);
        }
    }

    fprintf(stderr, "  └─ Allow all? [y/n/1-%d] ", count);
    fflush(stderr);
}
