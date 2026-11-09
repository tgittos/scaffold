#ifndef STATUS_LINE_H
#define STATUS_LINE_H

#include <stdbool.h>
#include <stddef.h>
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
 * Set the active model name and its context window (tokens; <= 0 = unknown).
 * NULL model clears the name; long names are truncated.
 */
void status_line_set_model(const char *model, int context_window);

/**
 * Mark the current context size as unknown until the next response reports
 * usage (e.g. after compaction trims the conversation).
 */
void status_line_invalidate_context(void);

/**
 * Build the "<pct>%/<window>  <model>" segment into buf (no terminal I/O).
 * Usage is "?" when unknown; the ctx part is yellow above 70% and red above
 * 90%; the model name is dim. buf is "" when neither model nor window is known.
 */
void status_line_format_context(char *buf, size_t buf_size);

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
 * Set the active prompt mode for display in the prompt string.
 * @param mode  PromptMode enum value (0 = default, no overlay)
 */
void status_line_set_mode(int mode);

/**
 * Build a single-line readline-compatible prompt string.
 * Returns a malloc'd bold "> " with ANSI codes wrapped in \001/\002.
 * Shows "[mode] > " when a non-default mode is active.
 * In JSON mode, returns a plain "> ".
 */
char *status_line_build_prompt(void);

#endif /* STATUS_LINE_H */
