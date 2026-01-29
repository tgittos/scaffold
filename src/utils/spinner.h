#ifndef SPINNER_H
#define SPINNER_H

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
 * Stop the pulsing spinner and clear the line.
 *
 * Clears the spinner line with \r\033[K to prepare for the result
 * display from log_tool_execution_improved().
 *
 * Safe to call even if spinner was never started.
 */
void spinner_stop(void);

/**
 * Cleanup spinner resources.
 *
 * Should be called during application shutdown to ensure thread
 * resources are released. Safe to call multiple times.
 */
void spinner_cleanup(void);

#endif /* SPINNER_H */
