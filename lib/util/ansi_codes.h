#ifndef ANSI_CODES_H
#define ANSI_CODES_H

/* =============================================================================
 * ANSI Color Codes
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

#endif /* ANSI_CODES_H */
