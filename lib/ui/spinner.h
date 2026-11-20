#ifndef SPINNER_H
#define SPINNER_H

#include <time.h>
#include <stddef.h>

/**
 * Pulsing spinner for local tool execution feedback.
 *
 * Provides visual feedback during tool execution by pulsing a cyan dot
 * on the terminal. Uses a background thread to animate independently
 * of the main execution.
 */

/**
 * Start the pulsing spinner with tool context.
 *
 * Displays a pulsing cyan dot followed by the tool name and a summary
 * of the arguments. The dot alternates between bright and dim cyan
 * every ~300ms.
 *
 * No-op in JSON output mode.
 *
 * @param tool_name Name of the tool being executed
 * @param arguments Tool arguments in JSON format (may be NULL)
 */
void spinner_start(const char *tool_name, const char *arguments);

/**
 * Set the live detail line shown by the spinner.
 *
 * Used to stream the last line of a running tool's output (e.g. shell
 * command progress) into the spinner. The text is displayed truncated to
 * the available width budget. Passing NULL or an empty string clears it.
 *
 * Thread-safe: may be called from a tool worker thread while the spinner
 * animates on its own thread.
 *
 * @param line Detail text to display (copied; may be NULL)
 */
void spinner_set_detail(const char *line);

/**
 * Stop the pulsing spinner and clear the line.
 *
 * Clears the spinner line with \r\033[K to prepare for the result
 * display from log_tool_execution_improved().
 *
 * Safe to call even if spinner was never started.
 */
void spinner_stop(void);

/**
 * Pause spinner frame rendering without stopping the background thread.
 *
 * Used by the REPL to avoid the spinner thread's raw fputs() racing with
 * readline's redisplay while the user is actively editing the input line
 * (e.g. during rl_callback_read_char()). The thread keeps running (elapsed
 * time keeps advancing) but skips writing frames to stdout until resumed.
 *
 * Safe to call even if the spinner was never started.
 */
void spinner_pause(void);

/**
 * Resume spinner frame rendering after spinner_pause().
 *
 * Safe to call even if the spinner was never started or is not paused.
 */
void spinner_resume(void);

/**
 * Cleanup spinner resources.
 *
 * Should be called during application shutdown to ensure thread
 * resources are released. Safe to call multiple times.
 */
void spinner_cleanup(void);

/* =============================================================================
 * Test hooks — not for use in production code.
 * =============================================================================
 */

/**
 * Injectable clock: defaults to time(NULL). Override in tests.
 * The function takes no arguments and returns the current time as time_t.
 */
typedef time_t (*spinner_clock_fn)(void);
void spinner_set_clock_for_test(spinner_clock_fn fn);

/**
 * Build one spinner frame into buf without outputting to stdout.
 * Useful for asserting elapsed-time formatting without background threads.
 *
 * @param buf        Output buffer.
 * @param buf_size   Size of buf.
 * @param tool_name  Tool name to embed in the frame.
 * @param arg_summary Argument summary string (may be NULL).
 * @param start_time  Spinner start time (uses g_clock_fn() for "now").
 */
void spinner_build_frame_for_test(char *buf, size_t buf_size,
                                   const char *tool_name,
                                   const char *arg_summary,
                                   time_t start_time);

/** Return non-zero if the spinner background thread is currently running. */
int spinner_is_running_for_test(void);

/** Return non-zero if spinner frame rendering is currently paused. */
int spinner_is_paused_for_test(void);

#endif /* SPINNER_H */
