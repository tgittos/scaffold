#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdio.h>
#include <stdbool.h>

/* =============================================================================
 * ANSI Color Codes - Single Source of Truth
 * =============================================================================
 */

#define TERM_RESET      "\033[0m"
#define TERM_BOLD       "\033[1m"
#define TERM_DIM        "\033[2m"

#define TERM_BLACK      "\033[30m"
#define TERM_RED        "\033[31m"
#define TERM_GREEN      "\033[32m"
#define TERM_YELLOW     "\033[33m"
#define TERM_BLUE       "\033[34m"
#define TERM_MAGENTA    "\033[35m"
#define TERM_CYAN       "\033[36m"
#define TERM_WHITE      "\033[37m"
#define TERM_GRAY       "\033[90m"

#define TERM_BRIGHT_RED     "\033[91m"
#define TERM_BRIGHT_GREEN   "\033[92m"
#define TERM_BRIGHT_YELLOW  "\033[93m"
#define TERM_BRIGHT_BLUE    "\033[94m"
#define TERM_BRIGHT_MAGENTA "\033[95m"
#define TERM_BRIGHT_CYAN    "\033[96m"

/* =============================================================================
 * Box-Drawing Characters
 * =============================================================================
 */

#define TERM_BOX_LIGHT_H    "\u2500"  /* ─ */
#define TERM_BOX_HEAVY_H    "\u2550"  /* ═ */
#define TERM_BOX_LIGHT_V    "\u2502"  /* │ */
#define TERM_BOX_HEAVY_V    "\u2551"  /* ║ */

/* Tree connectors */
#define TERM_TREE_BRANCH    "\u251C\u2500"  /* ├─ */
#define TERM_TREE_LAST      "\u2514\u2500"  /* └─ */
#define TERM_TREE_VERT      "\u2502"        /* │ */

/* Pre-rendered separators (40 chars wide) for inline use */
#define TERM_SEP_LIGHT_40   "\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500"
#define TERM_SEP_HEAVY_40   "\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550"

/* =============================================================================
 * Status Symbols
 * =============================================================================
 */

#define TERM_SYM_SUCCESS    "\u2713"  /* ✓ */
#define TERM_SYM_ERROR      "\u2717"  /* ✗ */
#define TERM_SYM_INFO       "\u25E6"  /* ◦ */
#define TERM_SYM_ACTIVE     "\u25CF"  /* ● */
#define TERM_SYM_BULLET     "\u2022"  /* • */

/* Control sequences */
#define TERM_CLEAR_LINE     "\r\033[K"  /* carriage return + clear to end of line */
#define TERM_CLEAR_SCREEN   "\033[J"    /* clear from cursor to end of screen */
#define TERM_CURSOR_UP_FMT  "\033[%dA"  /* move cursor up N lines (use with printf) */

/* =============================================================================
 * Enums
 * =============================================================================
 */

typedef enum {
    TERM_SEP_LIGHT,   /* ──────────── (thin line) */
    TERM_SEP_HEAVY    /* ════════════ (thick line) */
} TerminalSeparatorStyle;

typedef enum {
    TERM_STATUS_SUCCESS,  /* ✓ green */
    TERM_STATUS_ERROR,    /* ✗ red */
    TERM_STATUS_INFO,     /* ◦ yellow */
    TERM_STATUS_ACTIVE    /* ● cyan */
} TerminalStatusType;

/* =============================================================================
 * Function Declarations
 * =============================================================================
 */

/**
 * Check if terminal colors should be enabled.
 * Returns false if JSON output mode is active or stdout is not a TTY.
 */
bool terminal_colors_enabled(void);

/**
 * Print a separator line of specified width and style.
 *
 * @param out    Output stream (stdout/stderr)
 * @param style  TERM_SEP_LIGHT for thin line, TERM_SEP_HEAVY for thick
 * @param width  Number of characters wide
 */
void terminal_separator(FILE *out, TerminalSeparatorStyle style, int width);

/**
 * Print a header with title centered in a separator line.
 *
 * @param out    Output stream
 * @param title  Title text (can include emoji)
 * @param width  Total width of the header line
 */
void terminal_header(FILE *out, const char *title, int width);

/**
 * Print a tree item with appropriate connector.
 *
 * @param out      Output stream
 * @param text     Text to display after connector
 * @param is_last  True if this is the last item (uses └─), false for ├─
 * @param indent   Number of spaces before the connector
 */
void terminal_tree_item(FILE *out, const char *text, bool is_last, int indent);

/**
 * Print just the tree branch connector (for building custom lines).
 *
 * @param out      Output stream
 * @param is_last  True for └─, false for ├─
 * @param indent   Number of spaces before the connector
 */
void terminal_tree_branch(FILE *out, bool is_last, int indent);

/**
 * Print a status indicator with appropriate symbol and color.
 *
 * @param out      Output stream
 * @param type     Status type (determines symbol and color)
 * @param message  Message to display after the status symbol
 */
void terminal_status(FILE *out, TerminalStatusType type, const char *message);

/**
 * Print a status indicator with additional detail text.
 *
 * @param out      Output stream
 * @param type     Status type
 * @param message  Main message
 * @param detail   Additional detail (displayed dimmed in parentheses)
 */
void terminal_status_with_detail(FILE *out, TerminalStatusType type,
                                  const char *message, const char *detail);

/**
 * Print a labeled field (bold label, normal value).
 *
 * @param out    Output stream
 * @param label  Field label
 * @param value  Field value
 */
void terminal_labeled(FILE *out, const char *label, const char *value);

/**
 * Clear the current terminal line (for spinners/progress updates).
 *
 * @param out  Output stream
 */
void terminal_clear_line(FILE *out);

/**
 * Strip ANSI escape codes from a string.
 *
 * @param str  Input string (may contain ANSI codes)
 * @return     Newly allocated string with ANSI codes removed.
 *             Caller must free. Returns NULL if str is NULL or on allocation failure.
 */
char *terminal_strip_ansi(const char *str);

#endif /* TERMINAL_H */
