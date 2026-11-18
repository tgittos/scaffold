#ifndef TERMINAL_WIDTH_H
#define TERMINAL_WIDTH_H

#include <stddef.h>

/**
 * Return the terminal width in columns.
 *
 * Uses TIOCGWINSZ if stdout is a TTY, then falls back to the COLUMNS
 * environment variable.  Returns 80 when stdout is not a TTY and no other
 * source is available.
 *
 * The return value may be overridden in tests via
 * terminal_set_columns_for_test().
 */
int terminal_columns(void);

/**
 * Override the value returned by terminal_columns() in tests.
 *
 * Pass -1 to clear the override and restore normal detection.
 * Not for use in production code.
 */
void terminal_set_columns_for_test(int cols);

/**
 * Return the visible display width of a string, excluding ANSI CSI escape
 * sequences and counting UTF-8 code-points (not bytes).
 */
size_t visible_width(const char *s);

/**
 * Return the number of columns available for a tool's argument summary.
 *
 * The budget = terminal_columns() − prefix_width − 4, floored at 20.
 * prefix_width is the visible width of the prefix that precedes the summary
 * in the tool output line: the status symbol, a space, the tool name, and
 * the opening " (" — i.e. strlen(tool_name) + 4 visible columns.
 *
 * @param tool_name  Tool name string (may be NULL; treated as empty).
 */
size_t tool_summary_budget(const char *tool_name);

#endif /* TERMINAL_WIDTH_H */
