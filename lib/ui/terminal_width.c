#include "terminal_width.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

/* When >= 0, overrides the value returned by terminal_columns(). */
static int g_test_columns = -1;

void terminal_set_columns_for_test(int cols) {
    g_test_columns = cols;
}

int terminal_columns(void) {
    if (g_test_columns >= 0) {
        return g_test_columns;
    }
    if (!isatty(STDOUT_FILENO)) {
        /* Non-TTY: return the COLUMNS env-var if set, else 80. */
        const char *env = getenv("COLUMNS");
        if (env) {
            int cols = atoi(env);
            if (cols > 0) return cols;
        }
        return 80;
    }
    /* TTY: query the terminal. */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        return (int)ws.ws_col;
    }
    const char *env = getenv("COLUMNS");
    if (env) {
        int cols = atoi(env);
        if (cols > 0) return cols;
    }
    return 80;
}

size_t visible_width(const char *s) {
    if (!s) return 0;
    size_t width = 0;
    while (*s) {
        /* Skip CSI escape sequences (\033[...final). */
        if (s[0] == '\033' && s[1] == '[') {
            s += 2;
            while (*s && !(*s >= '@' && *s <= '~')) s++;
            if (*s) s++;
            continue;
        }
        /* Count only lead bytes of UTF-8 sequences (not continuation bytes). */
        if (((unsigned char)*s & 0xC0) != 0x80) width++;
        s++;
    }
    return width;
}

size_t tool_summary_budget(const char *tool_name) {
    int cols = terminal_columns();
    /* Visible prefix before the arg summary: "✓ tool_name (" = 1+1+name+2 */
    size_t tool_len = tool_name ? strlen(tool_name) : 0;
    size_t prefix_width = tool_len + 4;
    int budget = cols - (int)prefix_width - 4;
    if (budget < 20) budget = 20;
    return (size_t)budget;
}
