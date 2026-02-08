#ifndef STATUS_LINE_H
#define STATUS_LINE_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    const char *id;
    const char *task;
    time_t start_time;
} StatusAgentInfo;

void status_line_init(void);
void status_line_cleanup(void);

/**
 * Update agent display from caller-provided summaries.
 * Caller extracts data from SubagentManager before calling.
 */
void status_line_update_agents(int count, const StatusAgentInfo *agents);

/**
 * Mark the system as busy with a short activity label.
 * The status line will show "● label (Ns)" while busy.
 */
void status_line_set_busy(const char *label);

/**
 * Clear the busy state set by status_line_set_busy().
 */
void status_line_set_idle(void);

/**
 * Update token counters. prompt_tokens overwrites (tracks current context
 * size); completion_tokens accumulates (tracks total generated output).
 */
void status_line_update_tokens(int prompt_tokens, int completion_tokens);
void status_line_set_last_response_tokens(int tokens);

/**
 * Print the status info line to stdout (skipped in JSON mode).
 * Must only be called from the main (REPL) thread.
 */
void status_line_render_info(void);

/**
 * Clear previously rendered status info line from the terminal.
 * Must only be called from the main (REPL) thread.
 */
void status_line_clear_rendered(void);

/**
 * Build a single-line readline-compatible prompt string.
 * Returns a malloc'd bold "> " with ANSI codes wrapped in \001/\002.
 * In JSON mode, returns a plain "> ".
 */
char *status_line_build_prompt(void);

#endif /* STATUS_LINE_H */
